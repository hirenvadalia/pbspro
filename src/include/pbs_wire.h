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
#ifndef	_PBS_WIRE_H
#define	_PBS_WIRE_H
#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include "auth.h"

/* Generated by flatcc */
#include "pbs_ifl_builder.h"
#include "pbs_ifl_reader.h"
#include "pbs_ifl_verifier.h"

/* Convenient namespace macro to manage long namespace prefix for flatbuffer */
#define ns(x) FLATBUFFERS_WRAP_NAMESPACE(PBS_ifl, x)

typedef struct pbs_tcp_auth_data {
	int ctx_status;
	void *ctx;
	auth_def_t *def;
} pbs_tcp_auth_data_t;

typedef struct pbs_tcp_chan {
	// FIXME: fb buffer?
	pbs_tcp_auth_data_t auths[2];
} pbs_tcp_chan_t;

//FIXME: void dis_setup_chan(int, pbs_tcp_chan_t * (*)(int));
//FIXME: void dis_destroy_chan(int);

void wire_chan_set_ctx_status(int, int, int);
int wire_chan_get_ctx_status(int, int);
void wire_chan_set_authctx(int, void *, int);
void *wire_chan_get_authctx(int, int);
void wire_chan_set_authdef(int, auth_def_t *, int);
auth_def_t *wire_chan_get_authdef(int, int);
int wire_send_pkt(int, int, void *, size_t);
int wire_recv_pkt(int, int *, void **, size_t *);

pbs_tcp_chan_t *(*pfn_wire_get_chan)(int);
int (*pfn_wire_set_chan)(int, pbs_tcp_chan_t *);
int (*pfn_wire_recv)(int, void *, int);
int (*pfn_wire_send)(int, void *, int);

#define wire_recv(x, y, z) (*pfn_wire_recv)(x, y, z)
#define wire_send(x, y, z) (*pfn_wire_send)(x, y, z)
#define wire_get_chan(x) (*pfn_wire_get_chan)(x)
#define wire_set_chan(x, y) (*pfn_wire_set_chan)(x, y)

#ifdef	__cplusplus
}
#endif
#endif	/* _PBS_WIRE_H */
