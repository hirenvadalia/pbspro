/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */

#include <pbs_config.h>   /* the master config generated by configure */

#if defined(PBS_SECURITY) && (PBS_SECURITY == KRB5)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gssapi.h>
#include <time.h>
#include <unistd.h>

#include "libutil.h"
#include "pbs_ifl.h"
#include "log.h"
#include "pbs_gss.h"
#include "pbs_krb5.h"

char gss_log_buffer[LOG_BUF_SIZE];

#define DEFAULT_CREDENTIAL_LIFETIME 7200
char *gss_err_msg = "GSS - %s/%s";

void (*pbs_gss_log_gss_status)(const char *msg, OM_uint32 maj_stat, OM_uint32 min_stat);
void (*pbs_gss_logerror)(const char *func_name, const char* msg);
void (*pbs_gss_logdebug)(const char *func_name, const char* msg);

#if defined(KRB5_HEIMDAL)
#define PBS_GSS_MECH_OID GSS_KRB5_MECHANISM
#else
#include <gssapi/gssapi_krb5.h>
#define PBS_GSS_MECH_OID (gss_OID)gss_mech_krb5
#endif

/** @brief
 *	If oid set is null then create oid set. Once we have the oid set,
 *	the appropriate gss mechanism is added (e.g. kerberos).
 *
 * @param[in/out] oidset - oid set for change
 *
 * @return	int
 * @retval	PBS_GSS_OK on success
 * @retval	!= PBS_GSS_OK on error
 */
static int
pbs_gss_oidset_mech(gss_OID_set *oidset)
{
	OM_uint32 maj_stat;
	OM_uint32 min_stat;
	if (*oidset == GSS_C_NULL_OID_SET) {
		maj_stat = gss_create_empty_oid_set(&min_stat, oidset);
		if (maj_stat != GSS_S_COMPLETE) {
			if (pbs_gss_log_gss_status) {
				snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_create_empty_oid_set");
				pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
			}

			return PBS_GSS_ERR_OID;
		}
	}

	maj_stat = gss_add_oid_set_member(&min_stat, PBS_GSS_MECH_OID, oidset);
	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_add_oid_set_member");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}

		return PBS_GSS_ERR_OID;
	}

	return PBS_GSS_OK;
}

/** @brief
 *	Release oid set
 *
 * @param[in] oidset - oid set for releasing
 */
static void
pbs_gss_release_oidset(gss_OID_set *oidset)
{
	OM_uint32 maj_stat;
	OM_uint32 min_stat;

	maj_stat = gss_release_oid_set(&min_stat, oidset);
	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_oid_set");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}
	}
}

/** @brief
 *	Determines whether GSS credentials can be acquired
 *
 * @return	int
 * @retval	true if creds can be acquired
 * @retval	false if creds can not be acquired
 */
int
pbs_gss_can_get_creds()
{
	OM_uint32 maj_stat;
	OM_uint32 min_stat;
	OM_uint32 valid_sec = 0;
	gss_cred_id_t creds = GSS_C_NO_CREDENTIAL;
	gss_OID_set oidset = GSS_C_NO_OID_SET;

	if (pbs_gss_oidset_mech(&oidset) != PBS_GSS_OK)
		return 0;

	maj_stat = gss_acquire_cred(&min_stat, GSS_C_NO_NAME, GSS_C_INDEFINITE, oidset, GSS_C_INITIATE, &creds, NULL, &valid_sec);
	if (maj_stat == GSS_S_COMPLETE && creds != GSS_C_NO_CREDENTIAL)
		gss_release_cred(&min_stat, &creds);

	pbs_gss_release_oidset(&oidset);

/* There is a bug in old MIT implementation. It causes valid_sec is always 0.
 * The problem is fixed in version >= 1.14 */
	return (maj_stat == GSS_S_COMPLETE && valid_sec > 10);
}

/** @brief
 *	Allocates GSS extra structure.
 *
 * @return	gss_extra_t*
 * @retval	structure on success
 * @retval	NULL on error
 */
void *
pbs_gss_alloc_gss_extra(int mode)
{
	pbs_gss_extra_t *gss_extra = calloc(1, sizeof(pbs_gss_extra_t));
	if (!gss_extra) {
		if (pbs_gss_logerror)
			pbs_gss_logerror(__func__, "Out of memory allocating gss_extra");
		return NULL;
	}

	gss_extra->gssctx = GSS_C_NO_CONTEXT;
	gss_extra->gssctx_established = 0;
	gss_extra->ready = 0;
	gss_extra->confidential = 0;
	gss_extra->role = mode;
	gss_extra->hostname = NULL;
	gss_extra->init_client_ccache = 0;
	gss_extra->clientname = NULL;
	gss_extra->client_name.length = 0;
	gss_extra->client_name.value = NULL;
	gss_extra->establishing = 0;

	return gss_extra;
}

/** @brief
 *	Free GSS extra structure.
 *
 * @param[in] gss_extra - The structure with GSS data
 */
void
pbs_gss_free_gss_extra(void *extra)
{
	pbs_gss_extra_t *gss_extra = (pbs_gss_extra_t *)extra;
	OM_uint32 min_stat = 0;

	if (gss_extra == NULL)
		return;

	free(gss_extra->hostname);
	free(gss_extra->clientname);

	if (gss_extra->gssctx != GSS_C_NO_CONTEXT)
		(void)gss_delete_sec_context(&min_stat, &gss_extra->gssctx, GSS_C_NO_BUFFER);

	if (gss_extra->client_name.length)
		(void)gss_release_buffer(&min_stat, &gss_extra->client_name);

	free(gss_extra);
	gss_extra = NULL;
}

/** @brief
 *	Copy data from gss buffer into string and provides the length of the data.
 *
 * @param[in] tok - token with source data
 * @param[out] data - data to be filled
 * @param[out] len - length of data
 *
 * @return	int
 * @retval	PBS_GSS_OK on success
 * @retval	!= PBS_GSS_OK on error
 */
static int
pbs_gss_fill_data(gss_buffer_t tok, char **data, int *len)
{
	*data = malloc(tok->length);
	if (*data == NULL) {
		if (pbs_gss_logerror)
			pbs_gss_logerror(__func__, "malloc failure");

		return PBS_GSS_ERR_INTERNAL;
	}

	memcpy(*data, tok->value, tok->length);
	*len = tok->length;
	return PBS_GSS_OK;
}

/** @brief
 *	Imports a service name and acquires credentials for it. The service name
 *	is imported with gss_import_name, and service credentials are acquired
 *	with gss_acquire_cred.
 *
 * @param[in] service_name - the service name
 * @param[out] server_creds - the GSS-API service credentials
 *
 * @return	int
 * @retval	PBS_GSS_OK on success
 * @retval	!= PBS_GSS_OK on error
 */
static int
pbs_gss_server_acquire_creds(char *service_name, gss_cred_id_t* server_creds)
{
	gss_name_t server_name;
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	gss_OID_set oidset = GSS_C_NO_OID_SET;
	gss_buffer_desc name_buf;

	name_buf.value = service_name;
	name_buf.length = strlen(service_name) + 1;

	maj_stat = gss_import_name(&min_stat, &name_buf, GSS_NT_SERVICE_NAME, &server_name);

	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_import_name");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}

		return PBS_GSS_ERR_IMPORT_NAME;
	}

	if (pbs_gss_oidset_mech(&oidset) != PBS_GSS_OK)
		return PBS_GSS_ERR_OID;

	maj_stat = gss_acquire_cred(&min_stat, server_name, 0, oidset, GSS_C_ACCEPT, server_creds, NULL, NULL);

	pbs_gss_release_oidset(&oidset);

	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_acquire_cred");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}

		if (gss_release_name(&min_stat, &server_name) != GSS_S_COMPLETE) {
			if (pbs_gss_log_gss_status) {
				snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_name");
				pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
			}

			return PBS_GSS_ERR_INTERNAL;
		}

		return PBS_GSS_ERR_ACQUIRE_CREDS;
	}

	maj_stat = gss_release_name(&min_stat, &server_name);
	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_name");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}

		return PBS_GSS_ERR_INTERNAL;
	}

	return PBS_GSS_OK;
}

/* @brief
 *	Client part of GSS hadshake
 *
 * @param[in] service_name - GSS service name
 * @param[in] creds - client credentials
 * @param[in] oid - The security mechanism to use. GSS_C_NULL_OID for default
 * @param[in] gss_flags - Flags indicating additional services or parameters requested for the context.
 * @param[in/out] gss_context - this context is being established here
 * @param[out] ret_flags - Flags indicating additional services or parameters requested for the context.
 * @param[in] data_in - received GSS token data
 * @param[in] len_in - length of data_in
 * @param[out] data_out - GSS token data for transmitting
 * @param[out] len_out - length of data_out
 *
 * @return	int
 * @retval	PBS_GSS_OK on success
 * @retval	!= PBS_GSS_OK on error
 */
static int
pbs_gss_client_establish_context(char *service_name, gss_cred_id_t creds, gss_OID oid, OM_uint32 gss_flags, gss_ctx_id_t * gss_context, OM_uint32 *ret_flags, char* data_in, int len_in, char **data_out, int *len_out)
{
	gss_buffer_desc send_tok;
	gss_buffer_desc recv_tok;
	gss_buffer_desc *token_ptr;
	gss_name_t target_name;
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	OM_uint32 init_sec_maj_stat;
	OM_uint32 init_sec_min_stat = 0;

	send_tok.value = service_name;
	send_tok.length = strlen(service_name) ;
	maj_stat = gss_import_name(&min_stat, &send_tok, GSS_NT_SERVICE_NAME, &target_name);
	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_import_name");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}

		return PBS_GSS_ERR_IMPORT_NAME;
	}

	send_tok.value = NULL;
	send_tok.length = 0;

	recv_tok.value = (void *)data_in;
	recv_tok.length = len_in;

	if (recv_tok.length > 0)
		token_ptr = &recv_tok;
	else
		token_ptr = GSS_C_NO_BUFFER;

	init_sec_maj_stat = gss_init_sec_context(&init_sec_min_stat, creds ? creds : GSS_C_NO_CREDENTIAL, gss_context, target_name, oid, gss_flags, 0, NULL, token_ptr, NULL, &send_tok, ret_flags, NULL);

	if (send_tok.length != 0) {
		pbs_gss_fill_data(&send_tok, data_out, len_out);

		maj_stat = gss_release_buffer(&min_stat, &send_tok);
		if (maj_stat != GSS_S_COMPLETE) {
			if (pbs_gss_log_gss_status) {
				snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_buffer");
				pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
			}

			return PBS_GSS_ERR_INTERNAL;
		}
	}

	maj_stat = gss_release_name(&min_stat, &target_name);
	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_name");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}

		return PBS_GSS_ERR_INTERNAL;
	}

	if (init_sec_maj_stat != GSS_S_COMPLETE && init_sec_maj_stat != GSS_S_CONTINUE_NEEDED) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_init_sec_context");
			pbs_gss_log_gss_status(gss_log_buffer, init_sec_maj_stat, init_sec_min_stat);
		}

		if (*gss_context != GSS_C_NO_CONTEXT) {
			maj_stat = gss_delete_sec_context(&min_stat, gss_context, GSS_C_NO_BUFFER);
			if (maj_stat != GSS_S_COMPLETE) {
				if (pbs_gss_log_gss_status) {
					snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_delete_sec_context");
					pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
				}

				return PBS_GSS_ERR_CONTEXT_DELETE;
			}
		}

		return PBS_GSS_ERR_CONTEXT_INIT;
	}

	if (init_sec_maj_stat == GSS_S_CONTINUE_NEEDED)
		return PBS_GSS_CONTINUE_NEEDED;

	return PBS_GSS_OK;
}

/* @brief
 *	Server part of GSS hadshake
 *
 * @param[in] server_creds - server credentials
 * @param[in] client_creds - optional credentials, can be NULL
 * @param[in/out] gss_context - this context is being established here
 * @param[out] client_name - GSS client name
 * @param[out] ret_flags - Flags indicating additional services or parameters requested for the context.
 * @param[in] data_in - received GSS token data
 * @param[in] len_in - length of data_in
 * @param[out] data_out - GSS token data for transmitting
 * @param[out] len_out - length of data_out
 *
 * @return	int
 * @retval	PBS_GSS_OK on success
 * @retval	!= PBS_GSS_OK on error
 */
static int
pbs_gss_server_establish_context(gss_cred_id_t server_creds, gss_cred_id_t* client_creds, gss_ctx_id_t* gss_context, gss_buffer_t client_name, OM_uint32* ret_flags, char* data_in, int len_in, char **data_out, int *len_out)
{
	gss_buffer_desc send_tok;
	gss_buffer_desc recv_tok;
	gss_name_t client;
	gss_OID doid;
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	OM_uint32 acc_sec_maj_stat;
	OM_uint32 acc_sec_min_stat = 0;

	recv_tok.value = (void *)data_in;
	recv_tok.length = len_in;

	if (recv_tok.length == 0) {
		if (pbs_gss_logerror) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, "Establishing gss context failed. Failed to receive gss token.");
			pbs_gss_logerror(__func__, gss_log_buffer);
		}

		return PBS_GSS_ERR_RECVTOKEN;
	}

	acc_sec_maj_stat = gss_accept_sec_context(&acc_sec_min_stat, gss_context, server_creds, &recv_tok, GSS_C_NO_CHANNEL_BINDINGS, &client, &doid, &send_tok, ret_flags, NULL, client_creds);

	if (send_tok.length != 0) {
		pbs_gss_fill_data(&send_tok, data_out, len_out);

		maj_stat = gss_release_buffer(&min_stat, &send_tok);
		if (maj_stat != GSS_S_COMPLETE) {
			if (pbs_gss_log_gss_status) {
				snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_buffer");
				pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
			}

			return PBS_GSS_ERR_INTERNAL;
		}
	}

	if (acc_sec_maj_stat != GSS_S_COMPLETE && acc_sec_maj_stat != GSS_S_CONTINUE_NEEDED) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_accept_sec_context");
			pbs_gss_log_gss_status(gss_log_buffer, acc_sec_maj_stat, acc_sec_min_stat);
		}

		if (*gss_context != GSS_C_NO_CONTEXT) {
			if ((maj_stat = gss_delete_sec_context(&min_stat, gss_context, GSS_C_NO_BUFFER)) != GSS_S_COMPLETE) {
				if (pbs_gss_log_gss_status) {
					snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_delete_sec_context");
					pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
				}

				return PBS_GSS_ERR_CONTEXT_DELETE;
			}
		}

		return PBS_GSS_ERR_CONTEXT_ACCEPT;
	}

	maj_stat = gss_display_name(&min_stat, client, client_name, &doid);
	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_display_name");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}

		return PBS_GSS_ERR_NAME_CONVERT;
	}

	maj_stat = gss_release_name(&min_stat, &client);
	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_name");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}

		return PBS_GSS_ERR_INTERNAL;
	}

	if (acc_sec_maj_stat == GSS_S_CONTINUE_NEEDED)
		return PBS_GSS_CONTINUE_NEEDED;

	return PBS_GSS_OK;
}

/* @brief
 *	This is the main gss handshake function for asynchronous handshake.
 *	It has two branches: client and server. Once the handshake is finished
 *	the GSS structure is set to ready for un/wrapping.
 *
 *
 * @param[in] gss_extra - gss structure
 * @param[in] server_host  - server host fqdn
 * @param[in] data_in - received GSS token data
 * @param[in] len_in - length of data_in
 * @param[out] data_out - GSS token data for transmitting
 * @param[out] len_out - length of data_out
 *
 * @return
 */
int
__pbs_gss_establish_context(pbs_gss_extra_t *gss_extra, void *data_in, int len_in, void **data_out, int *len_out, char *ebuf, int ebufsz)
{
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	gss_ctx_id_t gss_context = GSS_C_NO_CONTEXT;
	static gss_cred_id_t server_creds = GSS_C_NO_CREDENTIAL;
	gss_cred_id_t creds = GSS_C_NO_CREDENTIAL;
	char *service_name = NULL;
	time_t now = time((time_t *)NULL);
	static time_t lastcredstime = 0;
	static time_t credlifetime = 0;
	OM_uint32 lifetime;
	OM_uint32 gss_flags;
	OM_uint32 ret_flags;
	gss_OID oid;
	gss_OID_set oidset = GSS_C_NO_OID_SET;
	int ret;

	if (gss_extra == NULL)
		return PBS_GSS_ERR_INTERNAL;

	if (gss_extra->gssctx_established)
		return PBS_GSS_OK; /* ctx already established*/

	if (gss_extra->role == PBS_GSS_ROLE_UNKNOWN)
		return PBS_GSS_ERR_INTERNAL;

	if (gss_extra->hostname == NULL)
		return PBS_GSS_ERR_INTERNAL;

	gss_context = gss_extra->gssctx;

	if (service_name == NULL) {
		service_name = (char *) malloc(strlen(PBS_KRB5_SERVICE_NAME) + 1 + strlen(gss_extra->hostname) + 1);
		if (service_name == NULL) {
			if (pbs_gss_logerror)
				pbs_gss_logerror(__func__, "malloc failure");

			return PBS_GSS_ERR_INTERNAL;
		}
		sprintf(service_name, "%s@%s", PBS_KRB5_SERVICE_NAME, gss_extra->hostname);
	}

	switch(gss_extra->role) {

		case PBS_GSS_CLIENT:
			if ((gss_extra->init_client_ccache) && (init_pbs_client_ccache_from_keytab(gss_log_buffer, LOG_BUF_SIZE))) {
				if (pbs_gss_logerror)
					pbs_gss_logerror(__func__, gss_log_buffer);

				return PBS_GSS_ERR_INIT_CLIENT_CCACHE;
			}

			if (pbs_gss_oidset_mech(&oidset) != PBS_GSS_OK)
				return PBS_GSS_ERR_OID;

			maj_stat = gss_acquire_cred(&min_stat, GSS_C_NO_NAME, GSS_C_INDEFINITE, oidset, GSS_C_INITIATE, &creds, NULL, NULL);

			pbs_gss_release_oidset(&oidset);

			if (maj_stat != GSS_S_COMPLETE) {
				if (pbs_gss_log_gss_status) {
					snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_acquire_cred");
					pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
				}

				return PBS_GSS_ERR_ACQUIRE_CREDS;
			}

			gss_flags = GSS_C_MUTUAL_FLAG | GSS_C_DELEG_FLAG | GSS_C_INTEG_FLAG | GSS_C_CONF_FLAG;
			oid = PBS_GSS_MECH_OID;

			ret = pbs_gss_client_establish_context(service_name, creds, oid, gss_flags, &gss_context, &ret_flags, data_in, len_in, (char **)data_out, len_out);

			if (gss_extra->init_client_ccache)
				clear_pbs_ccache_env();

			if (creds != GSS_C_NO_CREDENTIAL) {
				maj_stat = gss_release_cred(&min_stat, &creds);
				if (maj_stat != GSS_S_COMPLETE) {
					if (pbs_gss_log_gss_status) {
						snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_cred");
						pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
					}

					return PBS_GSS_ERR_INTERNAL;
				}
			}

			break;

		case PBS_GSS_SERVER:
			/* if credentials are old, try to get new ones. If we can't, keep the old
			 * ones since they're probably still valid and hope that
			 * we can get new credentials next time */
			if (now - lastcredstime > credlifetime) {
				gss_cred_id_t new_server_creds = GSS_C_NO_CREDENTIAL;

				if (pbs_gss_server_acquire_creds(service_name, &new_server_creds) != PBS_GSS_OK) {
					if (pbs_gss_logerror) {
						snprintf(gss_log_buffer, LOG_BUF_SIZE, "Failed to acquire server credentials for %s", service_name);
						pbs_gss_logerror(__func__, gss_log_buffer);
					}

					/* try again in 2 minutes */
					lastcredstime = now + 120;
				} else {
					lastcredstime = now;
					if (pbs_gss_logdebug) {
						snprintf(gss_log_buffer, LOG_BUF_SIZE, "Refreshing server credentials at %ld", (long)now);
						pbs_gss_logdebug(__func__, gss_log_buffer);
					}

					if (server_creds != GSS_C_NO_CREDENTIAL) {
						maj_stat = gss_release_cred(&min_stat, &server_creds);
						if (maj_stat != GSS_S_COMPLETE) {
							if (pbs_gss_log_gss_status) {
								snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_cred");
								pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
							}

							return PBS_GSS_ERR_INTERNAL;
						}
					}

					server_creds = new_server_creds;

					/* fetch information about the fresh credentials */
					if (gss_inquire_cred(&ret_flags, server_creds, NULL, &lifetime, NULL, NULL) == GSS_S_COMPLETE) {
						if (lifetime == GSS_C_INDEFINITE) {
							credlifetime = DEFAULT_CREDENTIAL_LIFETIME;
							if (pbs_gss_logdebug) {
								snprintf(gss_log_buffer, LOG_BUF_SIZE, "Server credentials renewed with indefinite lifetime, using %d.", DEFAULT_CREDENTIAL_LIFETIME);
								pbs_gss_logdebug(__func__, gss_log_buffer);
							}
						} else {
							if (pbs_gss_logdebug) {
								snprintf(gss_log_buffer, LOG_BUF_SIZE, "Server credentials renewed with lifetime as %u.", lifetime);
								pbs_gss_logdebug(__func__, gss_log_buffer);
							}
							credlifetime = lifetime;
						}
					} else {
						/* could not read information from credential */
						credlifetime = 0;
					}
				}
			}

			ret = pbs_gss_server_establish_context(server_creds, NULL, &gss_context, &(gss_extra->client_name), &ret_flags, data_in, len_in, (char **)data_out, len_out);

			break;

		default:
			return -1;
	}

	if (service_name != NULL)
		free(service_name);

	if (gss_context == GSS_C_NO_CONTEXT) {
		if (pbs_gss_logerror) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, "Failed to establish gss context");
			pbs_gss_logerror(__func__, gss_log_buffer);
		}

		return PBS_GSS_ERR_CONTEXT_ESTABLISH;
	}

	gss_extra->gssctx = gss_context;

	if (ret == PBS_GSS_CONTINUE_NEEDED) {
		return PBS_GSS_OK;
	}

	if (gss_extra->client_name.length) {
		gss_extra->clientname = malloc(gss_extra->client_name.length + 1);
		if (gss_extra->clientname == NULL) {
			if (pbs_gss_logerror)
				pbs_gss_logerror(__func__, "malloc failure");

			return PBS_GSS_ERR_INTERNAL;
		}

		memcpy(gss_extra->clientname, gss_extra->client_name.value, gss_extra->client_name.length);
		gss_extra->clientname[gss_extra->client_name.length] = '\0';
	}

	if (ret == PBS_GSS_OK) {
		gss_extra->gssctx_established = 1;
		gss_extra->confidential = (ret_flags & GSS_C_CONF_FLAG);
		if (pbs_gss_logdebug) {
			if (gss_extra->role == PBS_GSS_SERVER) {
				snprintf(gss_log_buffer, LOG_BUF_SIZE, "GSS context established with client %s", gss_extra->clientname);
			} else {
				snprintf(gss_log_buffer, LOG_BUF_SIZE, "GSS context established with server %s", gss_extra->hostname);
			}
			pbs_gss_logdebug(__func__, gss_log_buffer);
		}
	} else {
		if (pbs_gss_logerror) {
			if (gss_extra->role == PBS_GSS_SERVER) {
				if (gss_extra->clientname)
					snprintf(gss_log_buffer, LOG_BUF_SIZE, "Failed to establish GSS context with client %s", gss_extra->clientname);
				else
					snprintf(gss_log_buffer, LOG_BUF_SIZE, "Failed to establish GSS context with client");
			} else {
				snprintf(gss_log_buffer, LOG_BUF_SIZE, "Failed to establish GSS context with server %s", gss_extra->hostname);
			}
			pbs_gss_logerror(__func__, gss_log_buffer);
		}

		return PBS_GSS_ERR_CONTEXT_ESTABLISH;
	}

	return PBS_GSS_OK;
}

int
pbs_gss_establish_context(void *extra, void *data_in, int len_in, void **data_out, int *len_out, int *established, char *ebuf, int ebufsz)
{
	pbs_gss_extra_t *gss_extra = (pbs_gss_extra_t *) extra;
	int rc = 0;

	if (gss_extra->gssctx_established) {
		snprintf(ebuf, ebufsz, "GSS context already established");
		return -1;
	}

	rc = __pbs_gss_establish_context(gss_extra, data_in, len_in, data_out, len_out, ebuf, ebufsz);
	if (rc != PBS_GSS_OK)
		*established = 0;

	if (gss_extra->gssctx_established) {
		gss_extra->ready = 1;
		*established = 1;

		if (gss_extra->role == PBS_GSS_SERVER) {
			if (pbs_gss_logdebug) {
				snprintf(gss_log_buffer, LOG_BUF_SIZE, "Entered encrypted communication with client %s", gss_extra->clientname);
				pbs_gss_logdebug(__func__, gss_log_buffer);
			}
		} else {
			if (pbs_gss_logdebug) {
				snprintf(gss_log_buffer, LOG_BUF_SIZE, "Entered encrypted communication with server %s", gss_extra->hostname);
				pbs_gss_logdebug(__func__, gss_log_buffer);
			}
		}
	}

	return rc;
}

/* @brief
 *	The function wraps data based on established GSS context.
 *
 * @param[in] gss_extra - gss structure
 * @param[in] data_in - clear text data for wrapping
 * @param[in] len_in - length of data_in
 * @param[out] data_out - wrapped data
 * @param[out] len_out - length of data_out
 *
 * @return
 */
int
pbs_gss_wrap(void *extra, void *data_in, int len_in, void **data_out, int *len_out, char *ebuf, int ebufsz)
{
	pbs_gss_extra_t *gss_extra = (pbs_gss_extra_t *)extra;
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	gss_buffer_desc unwrapped;
	gss_buffer_desc wrapped;
	int conf_state = 0;

	if (gss_extra == NULL) {
		snprintf(ebuf, ebufsz, "No GSS auth extra available");
		return PBS_GSS_ERR_INTERNAL;
	}

	if (gss_extra->cleartext != NULL) {
		free(gss_extra->cleartext);
	}
	gss_extra->cleartext = malloc(len_in);
	if (gss_extra->cleartext == NULL) {
		snprintf(ebuf, ebufsz, "malloc failure");
		return PBS_GSS_ERR_INTERNAL;
	}
	memcpy(gss_extra->cleartext, data_in, len_in);
	gss_extra->cleartext_len = len_in;

	if (!gss_extra->ready) {
		snprintf(ebuf, ebufsz, "asked to wrap data but GSS layer not ready");
		return PBS_GSS_ERR_INTERNAL;
	}

	wrapped.length = 0;
	wrapped.value = NULL;

	unwrapped.length = len_in;
	unwrapped.value  = data_in;

	maj_stat = gss_wrap(&min_stat, gss_extra->gssctx, gss_extra->confidential, GSS_C_QOP_DEFAULT, &unwrapped, &conf_state, &wrapped);

	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_wrap");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}

		maj_stat = gss_release_buffer(&min_stat, &wrapped);
		if (maj_stat != GSS_S_COMPLETE) {
			if (pbs_gss_log_gss_status) {
				snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_buffer");
				pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
			}

			return PBS_GSS_ERR_INTERNAL;
		}

		return PBS_GSS_ERR_WRAP;
	}

	*len_out = wrapped.length;
	*data_out = malloc(wrapped.length);
	if (*data_out == NULL) {
		if (pbs_gss_logerror)
			pbs_gss_logerror(__func__, "malloc failure");

		return PBS_GSS_ERR_INTERNAL;
	}
	memcpy(*data_out, wrapped.value, wrapped.length);

	maj_stat = gss_release_buffer(&min_stat, &wrapped);
	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_buffer");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}


		return PBS_GSS_ERR_INTERNAL;
	}

	return PBS_GSS_OK;
}

/* @brief
 *	The function unwraps data based on established GSS context.
 *
 * @param[in] gss_extra - gss structure
 * @param[in] data_in - encrypted data for unwrapping
 * @param[in] len_in - length of data_in
 * @param[out] data_out - cleartext data
 * @param[out] len_out - length of data_out
 *
 * @return
 */
int
pbs_gss_unwrap(void *extra, void *data_in, int len_in, void **data_out, int *len_out, char *ebuf, int ebufsz)
{
	pbs_gss_extra_t *gss_extra = (pbs_gss_extra_t *)extra;
	OM_uint32 maj_stat;
	OM_uint32 min_stat = 0;
	gss_buffer_desc wrapped;
	gss_buffer_desc unwrapped;

	if (gss_extra == NULL) {
		snprintf(ebuf, ebufsz, "No GSS auth extra available");
		return PBS_GSS_ERR_INTERNAL;
	}

	if (gss_extra->ready == 0) {
		snprintf(ebuf, ebufsz, "wrapped data ready but GSS layer not ready");
		return PBS_GSS_ERR_INTERNAL;
	}

	if (gss_extra->confidential == 0) {
		snprintf(ebuf, ebufsz, "wrapped data ready but confidentiality not ensured");
		return PBS_GSS_ERR_INTERNAL;
	}

	if (len_in == 0 && data_in == NULL) {
		if (gss_extra->cleartext == NULL) {
			snprintf(ebuf, ebufsz, "No cleartext data available in gss auth extra");
			return PBS_GSS_ERR_INTERNAL;
		}
		*data_out = malloc(gss_extra->cleartext_len);
		if (*data_out == NULL) {
			snprintf(ebuf, ebufsz, "malloc failure");
			return PBS_GSS_ERR_INTERNAL;
		}
		memcpy(*data_out, gss_extra->cleartext, gss_extra->cleartext_len);
		*len_out = gss_extra->cleartext_len;
		free(gss_extra->cleartext);
		gss_extra->cleartext = NULL;
		gss_extra->cleartext_len = 0;
		return PBS_GSS_OK;
	}

	unwrapped.length = 0;
	unwrapped.value = NULL;

	wrapped.length = len_in;
	wrapped.value = data_in;

	maj_stat = gss_unwrap(&min_stat, gss_extra->gssctx, &wrapped, &unwrapped, NULL, NULL);

	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_unwrap");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}

		maj_stat = gss_release_buffer(&min_stat, &unwrapped);
		if (maj_stat != GSS_S_COMPLETE) {
			if (pbs_gss_log_gss_status) {
				snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_buffer");
				pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
			}

			return PBS_GSS_ERR_INTERNAL;
		}

		return PBS_GSS_ERR_UNWRAP;
	}

	if (unwrapped.length == 0)
		return PBS_GSS_ERR_UNWRAP;

	*len_out = unwrapped.length;
	*data_out = malloc(unwrapped.length);
	if (*data_out == NULL) {
		if (pbs_gss_logerror)
			pbs_gss_logerror(__func__, "malloc failure");

		return PBS_GSS_ERR_INTERNAL;
	}
	memcpy(*data_out, unwrapped.value, unwrapped.length);

	maj_stat = gss_release_buffer(&min_stat, &unwrapped);
	if (maj_stat != GSS_S_COMPLETE) {
		if (pbs_gss_log_gss_status) {
			snprintf(gss_log_buffer, LOG_BUF_SIZE, gss_err_msg, __func__, "gss_release_buffer");
			pbs_gss_log_gss_status(gss_log_buffer, maj_stat, min_stat);
		}

		return PBS_GSS_ERR_INTERNAL;
	}

	return PBS_GSS_OK;
}

/* @brief
 *	Function to register the log handler functions
 *
 * @param[in] log_gss_status - function ptr to function that logs GSS error codes
 * @param[in] logerror - function ptr to function that logs error
 * @param[in] logdebug - function ptr to function that logs debug messages
 *
 * @return
 */
void
pbs_gss_set_log_handlers(void (*log_gss_status)(const char *msg, OM_uint32 maj_stat, OM_uint32 min_stat),
	void (*logerror)(const char *func_name, const char* msg),
	void (*logdebug)(const char *func_name, const char* msg))
{
	pbs_gss_log_gss_status = log_gss_status;
	pbs_gss_logerror = logerror;
	pbs_gss_logdebug = logdebug;
}
#endif
