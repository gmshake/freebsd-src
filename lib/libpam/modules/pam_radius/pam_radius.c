/*-
 * Copyright 1998 Juniper Networks, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD: src/lib/libpam/modules/pam_radius/pam_radius.c,v 1.2 1999/01/20 21:55:26 jdp Exp $
 */

#include <sys/param.h>
#include <pwd.h>
#include <radlib.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define PAM_SM_AUTH
#include <security/pam_modules.h>

#include "pam_mod_misc.h"

#define MAX_CHALLENGE_MSGS	10
#define PASSWORD_PROMPT	"RADIUS password:"

/* Option names, including the "=" sign. */
#define OPT_CONF		"conf="
#define OPT_TMPL		"template_user="

static int	 build_access_request(struct rad_handle *, const char *,
		    const char *, const void *, size_t);
static int	 do_accept(pam_handle_t *, struct rad_handle *);
static int	 do_challenge(pam_handle_t *, struct rad_handle *,
		    const char *);

/*
 * Construct an access request, but don't send it.  Returns 0 on success,
 * -1 on failure.
 */
static int
build_access_request(struct rad_handle *radh, const char *user,
    const char *pass, const void *state, size_t state_len)
{
	char	 host[MAXHOSTNAMELEN];

	if (rad_create_request(radh, RAD_ACCESS_REQUEST) == -1) {
		syslog(LOG_CRIT, "rad_create_request: %s", rad_strerror(radh));
		return -1;
	}
	if ((user != NULL &&
	    rad_put_string(radh, RAD_USER_NAME, user) == -1) ||
	    (pass != NULL &&
	    rad_put_string(radh, RAD_USER_PASSWORD, pass) == -1) ||
	    (gethostname(host, sizeof host) != -1 &&
	    rad_put_string(radh, RAD_NAS_IDENTIFIER, host) == -1)) {
		syslog(LOG_CRIT, "rad_put_string: %s", rad_strerror(radh));
		return -1;
	}
	if (state != NULL && rad_put_attr(radh, RAD_STATE, state,
	    state_len) == -1) {
		syslog(LOG_CRIT, "rad_put_attr: %s", rad_strerror(radh));
		return -1;
	}
	if (rad_put_int(radh, RAD_SERVICE_TYPE, RAD_AUTHENTICATE_ONLY) == -1) {
		syslog(LOG_CRIT, "rad_put_int: %s", rad_strerror(radh));
		return -1;
	}
	return 0;
}

static int
do_accept(pam_handle_t *pamh, struct rad_handle *radh)
{
	int attrtype;
	const void *attrval;
	size_t attrlen;
	char *s;

	while ((attrtype = rad_get_attr(radh, &attrval, &attrlen)) > 0) {
		if (attrtype == RAD_USER_NAME) {
			s = rad_cvt_string(attrval, attrlen);
			if (s == NULL) {
				syslog(LOG_CRIT,
				    "rad_cvt_string: out of memory");
				return -1;
			}
			pam_set_item(pamh, PAM_USER, s);
			free(s);
		}
	}
	if (attrtype == -1) {
		syslog(LOG_CRIT, "rad_get_attr: %s", rad_strerror(radh));
		return -1;
	}
	return 0;
}

static int
do_challenge(pam_handle_t *pamh, struct rad_handle *radh, const char *user)
{
	int retval;
	int attrtype;
	const void *attrval;
	size_t attrlen;
	const void *state;
	size_t statelen;
	struct pam_message msgs[MAX_CHALLENGE_MSGS];
	const struct pam_message *msg_ptrs[MAX_CHALLENGE_MSGS];
	struct pam_response *resp;
	int num_msgs;
	const void *item;
	const struct pam_conv *conv;

	state = NULL;
	statelen = 0;
	num_msgs = 0;
	while ((attrtype = rad_get_attr(radh, &attrval, &attrlen)) > 0) {
		switch (attrtype) {

		case RAD_STATE:
			state = attrval;
			statelen = attrlen;
			break;

		case RAD_REPLY_MESSAGE:
			if (num_msgs >= MAX_CHALLENGE_MSGS) {
				syslog(LOG_CRIT,
				    "Too many RADIUS challenge messages");
				return PAM_SERVICE_ERR;
			}
			msgs[num_msgs].msg = rad_cvt_string(attrval, attrlen);
			if (msgs[num_msgs].msg == NULL) {
				syslog(LOG_CRIT,
				    "rad_cvt_string: out of memory");
				return PAM_SERVICE_ERR;
			}
			msgs[num_msgs].msg_style = PAM_TEXT_INFO;
			msg_ptrs[num_msgs] = &msgs[num_msgs];
			num_msgs++;
			break;
		}
	}
	if (attrtype == -1) {
		syslog(LOG_CRIT, "rad_get_attr: %s", rad_strerror(radh));
		return PAM_SERVICE_ERR;
	}
	if (num_msgs == 0) {
		msgs[num_msgs].msg = strdup("(null RADIUS challenge): ");
		if (msgs[num_msgs].msg == NULL) {
			syslog(LOG_CRIT, "Out of memory");
			return PAM_SERVICE_ERR;
		}
		msgs[num_msgs].msg_style = PAM_TEXT_INFO;
		msg_ptrs[num_msgs] = &msgs[num_msgs];
		num_msgs++;
	}
	msgs[num_msgs-1].msg_style = PAM_PROMPT_ECHO_ON;
	if ((retval = pam_get_item(pamh, PAM_CONV, &item)) != PAM_SUCCESS) {
		syslog(LOG_CRIT, "do_challenge: cannot get PAM_CONV");
		return retval;
	}
	conv = (const struct pam_conv *)item;
	if ((retval = conv->conv(num_msgs, msg_ptrs, &resp,
	    conv->appdata_ptr)) != PAM_SUCCESS)
		return retval;
	if (build_access_request(radh, user, resp[num_msgs-1].resp, state,
	    statelen) == -1)
		return PAM_SERVICE_ERR;
	memset(resp[num_msgs-1].resp, 0, strlen(resp[num_msgs-1].resp));
	free(resp[num_msgs-1].resp);
	free(resp);
	while (num_msgs > 0)
		free((void *)msgs[--num_msgs].msg);
	return PAM_SUCCESS;
}

PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc,
    const char **argv)
{
	struct rad_handle *radh;
	const char *user;
	const char *pass;
	const char *conf_file = NULL;
	const char *template_user = NULL;
	int options = 0;
	int retval;
	int i;
	int e;

	for (i = 0;  i < argc;  i++) {
		size_t len;

		pam_std_option(&options, argv[i]);
		if (strncmp(argv[i], OPT_CONF, (len = strlen(OPT_CONF))) == 0)
			conf_file = argv[i] + len;
		else if (strncmp(argv[i], OPT_TMPL,
		    (len = strlen(OPT_TMPL))) == 0)
			template_user = argv[i] + len;
	}
	if ((retval = pam_get_user(pamh, &user, NULL)) != PAM_SUCCESS)
		return retval;
	if ((retval = pam_get_pass(pamh, &pass, PASSWORD_PROMPT,
	    options)) != PAM_SUCCESS)
		return retval;

	if ((radh = rad_open()) == NULL) {
		syslog(LOG_CRIT, "rad_open failed");
		return PAM_SERVICE_ERR;
	}
	if (rad_config(radh, conf_file) == -1) {
		syslog(LOG_ALERT, "rad_config: %s", rad_strerror(radh));
		rad_close(radh);
		return PAM_SERVICE_ERR;
	}
	if (build_access_request(radh, user, pass, NULL, 0) == -1) {
		rad_close(radh);
		return PAM_SERVICE_ERR;
	}
	for ( ; ; ) {
		switch (rad_send_request(radh)) {

		case RAD_ACCESS_ACCEPT:
			e = do_accept(pamh, radh);
			rad_close(radh);
			if (e == -1)
				return PAM_SERVICE_ERR;
			if (template_user != NULL) {
				const void *item;
				const char *user;

				/*
				 * If the given user name doesn't exist in
				 * the local password database, change it
				 * to the value given in the "template_user"
				 * option.
				 */
				retval = pam_get_item(pamh, PAM_USER, &item);
				if (retval != PAM_SUCCESS)
					return retval;
				user = (const char *)item;
				if (getpwnam(user) == NULL)
					pam_set_item(pamh, PAM_USER,
					    template_user);
			}
			return PAM_SUCCESS;

		case RAD_ACCESS_REJECT:
			rad_close(radh);
			return PAM_AUTH_ERR;

		case RAD_ACCESS_CHALLENGE:
			if ((retval = do_challenge(pamh, radh, user)) !=
			    PAM_SUCCESS) {
				rad_close(radh);
				return retval;
			}
			break;

		case -1:
			syslog(LOG_CRIT, "rad_send_request: %s",
			    rad_strerror(radh));
			rad_close(radh);
			return PAM_AUTHINFO_UNAVAIL;

		default:
			syslog(LOG_CRIT,
			    "rad_send_request: unexpected return value");
			rad_close(radh);
			return PAM_SERVICE_ERR;
		}
	}
}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	return PAM_SUCCESS;
}

PAM_MODULE_ENTRY("pam_radius");
