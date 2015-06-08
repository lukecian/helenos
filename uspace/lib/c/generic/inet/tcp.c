/*
 * Copyright (c) 2015 Jiri Svoboda
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup libc
 * @{
 */
/** @file TCP API
 */

#include <errno.h>
#include <fibril.h>
#include <inet/endpoint.h>
#include <inet/tcp.h>
#include <ipc/services.h>
#include <ipc/tcp.h>
#include <stdlib.h>

static void tcp_cb_conn(ipc_callid_t, ipc_call_t *, void *);
static int tcp_conn_fibril(void *);

/** Incoming TCP connection info */
typedef struct {
	tcp_listener_t *lst;
	tcp_conn_t *conn;
} tcp_in_conn_t;

static int tcp_callback_create(tcp_t *tcp)
{
	async_exch_t *exch = async_exchange_begin(tcp->sess);

	aid_t req = async_send_0(exch, TCP_CALLBACK_CREATE, NULL);
	int rc = async_connect_to_me(exch, 0, 0, 0, tcp_cb_conn, tcp);
	async_exchange_end(exch);

	if (rc != EOK)
		return rc;

	sysarg_t retval;
	async_wait_for(req, &retval);

	return retval;
}

int tcp_create(tcp_t **rtcp)
{
	tcp_t *tcp;
	service_id_t tcp_svcid;
	int rc;

	tcp = calloc(1, sizeof(tcp_t));
	if (tcp == NULL) {
		rc = ENOMEM;
		goto error;
	}

	list_initialize(&tcp->conn);
	list_initialize(&tcp->listener);

	rc = loc_service_get_id(SERVICE_NAME_TCP, &tcp_svcid,
	    IPC_FLAG_BLOCKING);
	if (rc != EOK) {
		rc = EIO;
		goto error;
	}

	tcp->sess = loc_service_connect(EXCHANGE_SERIALIZE, tcp_svcid,
	    IPC_FLAG_BLOCKING);
	if (tcp->sess == NULL) {
		rc = EIO;
		goto error;
	}

	rc = tcp_callback_create(tcp);
	if (rc != EOK) {
		rc = EIO;
		goto error;
	}

	*rtcp = tcp;
	return EOK;
error:
	free(tcp);
	return rc;
}

void tcp_destroy(tcp_t *tcp)
{
	if (tcp == NULL)
		return;

	async_hangup(tcp->sess);
	free(tcp);
}

static int tcp_conn_new(tcp_t *tcp, sysarg_t id, tcp_cb_t *cb, void *arg,
    tcp_conn_t **rconn)
{
	tcp_conn_t *conn;

	conn = calloc(1, sizeof(tcp_conn_t));
	if (conn == NULL)
		return ENOMEM;

	conn->data_avail = false;
	fibril_mutex_initialize(&conn->lock);
	fibril_condvar_initialize(&conn->cv);

	conn->tcp = tcp;
	conn->id = id;
	conn->cb = cb;
	conn->cb_arg = arg;

	list_append(&conn->ltcp, &tcp->conn);
	*rconn = conn;

	return EOK;
}

int tcp_conn_create(tcp_t *tcp, inet_ep2_t *epp, tcp_cb_t *cb, void *arg,
    tcp_conn_t **rconn)
{
	async_exch_t *exch;
	ipc_call_t answer;
	sysarg_t conn_id;

	exch = async_exchange_begin(tcp->sess);
	aid_t req = async_send_0(exch, TCP_CONN_CREATE, &answer);
	sysarg_t rc = async_data_write_start(exch, (void *)epp,
	    sizeof(inet_ep2_t));
	async_exchange_end(exch);

	if (rc != EOK) {
		sysarg_t rc_orig;
		async_wait_for(req, &rc_orig);
		if (rc_orig != EOK)
			rc = rc_orig;
		goto error;
	}

	async_wait_for(req, &rc);
	if (rc != EOK)
		goto error;

	conn_id = IPC_GET_ARG1(answer);

	rc = tcp_conn_new(tcp, conn_id, cb, arg, rconn);
	if (rc != EOK)
		return rc;

	return EOK;
error:
	return (int) rc;
}

void tcp_conn_destroy(tcp_conn_t *conn)
{
	async_exch_t *exch;

	if (conn == NULL)
		return;

	list_remove(&conn->ltcp);

	exch = async_exchange_begin(conn->tcp->sess);
	sysarg_t rc = async_req_1_0(exch, TCP_CONN_DESTROY, conn->id);
	async_exchange_end(exch);

	free(conn);
	(void) rc;
}

static int tcp_conn_get(tcp_t *tcp, sysarg_t id, tcp_conn_t **rconn)
{
	list_foreach(tcp->conn, ltcp, tcp_conn_t, conn) {
		if (conn->id == id) {
			*rconn = conn;
			return EOK;
		}
	}

	return EINVAL;
}

void *tcp_conn_userptr(tcp_conn_t *conn)
{
	return conn->cb_arg;
}

int tcp_listener_create(tcp_t *tcp, inet_ep_t *ep, tcp_listen_cb_t *lcb,
    void *larg, tcp_cb_t *cb, void *arg, tcp_listener_t **rlst)
{
	async_exch_t *exch;
	tcp_listener_t *lst;
	ipc_call_t answer;

	lst = calloc(1, sizeof(tcp_listener_t));
	if (lst == NULL)
		return ENOMEM;

	exch = async_exchange_begin(tcp->sess);
	aid_t req = async_send_0(exch, TCP_LISTENER_CREATE, &answer);
	sysarg_t rc = async_data_write_start(exch, (void *)ep,
	    sizeof(inet_ep_t));
	async_exchange_end(exch);

	if (rc != EOK) {
		sysarg_t rc_orig;
		async_wait_for(req, &rc_orig);
		if (rc_orig != EOK)
			rc = rc_orig;
		goto error;
	}

	async_wait_for(req, &rc);
	if (rc != EOK)
		goto error;

	lst->tcp = tcp;
	lst->id = IPC_GET_ARG1(answer);
	lst->lcb = lcb;
	lst->lcb_arg = larg;
	lst->cb = cb;
	lst->cb_arg = arg;

	list_append(&lst->ltcp, &tcp->listener);
	*rlst = lst;

	return EOK;
error:
	free(lst);
	return (int) rc;
}

void tcp_listener_destroy(tcp_listener_t *lst)
{
	async_exch_t *exch;

	if (lst == NULL)
		return;

	list_remove(&lst->ltcp);

	exch = async_exchange_begin(lst->tcp->sess);
	sysarg_t rc = async_req_1_0(exch, TCP_LISTENER_DESTROY, lst->id);
	async_exchange_end(exch);

	free(lst);
	(void) rc;
}

static int tcp_listener_get(tcp_t *tcp, sysarg_t id, tcp_listener_t **rlst)
{
	list_foreach(tcp->listener, ltcp, tcp_listener_t, lst) {
		if (lst->id == id) {
			*rlst = lst;
			return EOK;
		}
	}

	return EINVAL;
}

void *tcp_listener_userptr(tcp_listener_t *lst)
{
	return lst->lcb_arg;
}

int tcp_conn_wait_connected(tcp_conn_t *conn)
{
	fibril_mutex_lock(&conn->lock);
	while (!conn->connected && !conn->conn_failed && !conn->conn_reset)
		fibril_condvar_wait(&conn->cv, &conn->lock);

	if (conn->connected) {
		fibril_mutex_unlock(&conn->lock);
		return EOK;
	} else {
		assert(conn->conn_failed || conn->conn_reset);
		fibril_mutex_unlock(&conn->lock);
		return EIO;
	}
}

int tcp_conn_send(tcp_conn_t *conn, const void *data, size_t bytes)
{
	async_exch_t *exch;
	sysarg_t rc;

	exch = async_exchange_begin(conn->tcp->sess);
	aid_t req = async_send_1(exch, TCP_CONN_SEND, conn->id, NULL);

	rc = async_data_write_start(exch, data, bytes);
	if (rc != EOK) {
		async_forget(req);
		return rc;
	}

	async_exchange_end(exch);

	if (rc != EOK) {
		async_forget(req);
		return rc;
	}

	async_wait_for(req, &rc);
	return rc;
}


int tcp_conn_send_fin(tcp_conn_t *conn)
{
	async_exch_t *exch;

	exch = async_exchange_begin(conn->tcp->sess);
	sysarg_t rc = async_req_1_0(exch, TCP_CONN_SEND_FIN, conn->id);
	async_exchange_end(exch);

	return rc;
}

int tcp_conn_push(tcp_conn_t *conn)
{
	async_exch_t *exch;

	exch = async_exchange_begin(conn->tcp->sess);
	sysarg_t rc = async_req_1_0(exch, TCP_CONN_PUSH, conn->id);
	async_exchange_end(exch);

	return rc;
}

int tcp_conn_reset(tcp_conn_t *conn)
{
	async_exch_t *exch;

	exch = async_exchange_begin(conn->tcp->sess);
	sysarg_t rc = async_req_1_0(exch, TCP_CONN_RESET, conn->id);
	async_exchange_end(exch);

	return rc;
}

int tcp_conn_recv(tcp_conn_t *conn, void *buf, size_t bsize, size_t *nrecv)
{
	async_exch_t *exch;
	ipc_call_t answer;

	fibril_mutex_lock(&conn->lock);
	if (!conn->data_avail) {
		fibril_mutex_unlock(&conn->lock);
		return EAGAIN;
	}

	exch = async_exchange_begin(conn->tcp->sess);
	aid_t req = async_send_1(exch, TCP_CONN_RECV, conn->id, &answer);
	int rc = async_data_read_start(exch, buf, bsize);
	async_exchange_end(exch);

	if (rc != EOK) {
		async_forget(req);
		fibril_mutex_unlock(&conn->lock);
		return rc;
	}

	sysarg_t retval;
	async_wait_for(req, &retval);
	if (retval != EOK) {
		fibril_mutex_unlock(&conn->lock);
		return retval;
	}

	*nrecv = IPC_GET_ARG1(answer);
	fibril_mutex_unlock(&conn->lock);
	return EOK;
}

int tcp_conn_recv_wait(tcp_conn_t *conn, void *buf, size_t bsize, size_t *nrecv)
{
	async_exch_t *exch;
	ipc_call_t answer;

again:
	fibril_mutex_lock(&conn->lock);
	while (!conn->data_avail) {
		fibril_condvar_wait(&conn->cv, &conn->lock);
	}

	exch = async_exchange_begin(conn->tcp->sess);
	aid_t req = async_send_1(exch, TCP_CONN_RECV_WAIT, conn->id, &answer);
	int rc = async_data_read_start(exch, buf, bsize);
	async_exchange_end(exch);

	if (rc != EOK) {
		async_forget(req);
		if (rc == EAGAIN) {
			conn->data_avail = false;
			fibril_mutex_unlock(&conn->lock);
			goto again;
		}
		fibril_mutex_unlock(&conn->lock);
		return rc;
	}

	sysarg_t retval;
	async_wait_for(req, &retval);
	if (retval != EOK) {
		if (rc == EAGAIN) {
			conn->data_avail = false;
		}
		fibril_mutex_unlock(&conn->lock);
		return retval;
	}

	*nrecv = IPC_GET_ARG1(answer);
	fibril_mutex_unlock(&conn->lock);
	return EOK;
}

static void tcp_ev_connected(tcp_t *tcp, ipc_callid_t iid, ipc_call_t *icall)
{
	tcp_conn_t *conn;
	sysarg_t conn_id;
	int rc;

	conn_id = IPC_GET_ARG1(*icall);

	rc = tcp_conn_get(tcp, conn_id, &conn);
	if (rc != EOK) {
		async_answer_0(iid, ENOENT);
		return;
	}

	fibril_mutex_lock(&conn->lock);
	conn->connected = true;
	fibril_condvar_broadcast(&conn->cv);
	fibril_mutex_unlock(&conn->lock);

	async_answer_0(iid, EOK);
}

static void tcp_ev_conn_failed(tcp_t *tcp, ipc_callid_t iid, ipc_call_t *icall)
{
	tcp_conn_t *conn;
	sysarg_t conn_id;
	int rc;

	conn_id = IPC_GET_ARG1(*icall);

	rc = tcp_conn_get(tcp, conn_id, &conn);
	if (rc != EOK) {
		async_answer_0(iid, ENOENT);
		return;
	}

	fibril_mutex_lock(&conn->lock);
	conn->conn_failed = true;
	fibril_condvar_broadcast(&conn->cv);
	fibril_mutex_unlock(&conn->lock);

	async_answer_0(iid, EOK);
}

static void tcp_ev_conn_reset(tcp_t *tcp, ipc_callid_t iid, ipc_call_t *icall)
{
	tcp_conn_t *conn;
	sysarg_t conn_id;
	int rc;

	conn_id = IPC_GET_ARG1(*icall);

	rc = tcp_conn_get(tcp, conn_id, &conn);
	if (rc != EOK) {
		async_answer_0(iid, ENOENT);
		return;
	}

	fibril_mutex_lock(&conn->lock);
	conn->conn_reset = true;
	fibril_condvar_broadcast(&conn->cv);
	fibril_mutex_unlock(&conn->lock);

	async_answer_0(iid, EOK);
}

static void tcp_ev_data(tcp_t *tcp, ipc_callid_t iid, ipc_call_t *icall)
{
	tcp_conn_t *conn;
	sysarg_t conn_id;
	int rc;

	conn_id = IPC_GET_ARG1(*icall);

	rc = tcp_conn_get(tcp, conn_id, &conn);
	if (rc != EOK) {
		async_answer_0(iid, ENOENT);
		return;
	}

	conn->data_avail = true;
	fibril_condvar_broadcast(&conn->cv);

	if (conn->cb != NULL && conn->cb->data_avail != NULL)
		conn->cb->data_avail(conn);

	async_answer_0(iid, EOK);
}

static void tcp_ev_urg_data(tcp_t *tcp, ipc_callid_t iid, ipc_call_t *icall)
{
	async_answer_0(iid, ENOTSUP);
}

static void tcp_ev_new_conn(tcp_t *tcp, ipc_callid_t iid, ipc_call_t *icall)
{
	tcp_listener_t *lst;
	tcp_conn_t *conn;
	sysarg_t lst_id;
	sysarg_t conn_id;
	fid_t fid;
	tcp_in_conn_t *cinfo;
	int rc;

	lst_id = IPC_GET_ARG1(*icall);
	conn_id = IPC_GET_ARG2(*icall);

	rc = tcp_listener_get(tcp, lst_id, &lst);
	if (rc != EOK) {
		async_answer_0(iid, ENOENT);
		return;
	}

	rc = tcp_conn_new(tcp, conn_id, lst->cb, lst->cb_arg, &conn);
	if (rc != EOK) {
		async_answer_0(iid, ENOMEM);
		return;
	}

	if (lst->lcb != NULL && lst->lcb->new_conn != NULL) {
		cinfo = calloc(1, sizeof(tcp_in_conn_t));
		if (cinfo == NULL) {
			async_answer_0(iid, ENOMEM);
			return;
		}

		cinfo->lst = lst;
		cinfo->conn = conn;

		fid = fibril_create(tcp_conn_fibril, cinfo);
		if (fid == 0) {
			async_answer_0(iid, ENOMEM);
		}

		fibril_add_ready(fid);
	}

	async_answer_0(iid, EOK);
}

static void tcp_cb_conn(ipc_callid_t iid, ipc_call_t *icall, void *arg)
{
	tcp_t *tcp = (tcp_t *)arg;

	async_answer_0(iid, EOK);

	while (true) {
		ipc_call_t call;
		ipc_callid_t callid = async_get_call(&call);

		if (!IPC_GET_IMETHOD(call)) {
			/* TODO: Handle hangup */
			return;
		}

		switch (IPC_GET_IMETHOD(call)) {
		case TCP_EV_CONNECTED:
			tcp_ev_connected(tcp, callid, &call);
			break;
		case TCP_EV_CONN_FAILED:
			tcp_ev_conn_failed(tcp, callid, &call);
			break;
		case TCP_EV_CONN_RESET:
			tcp_ev_conn_reset(tcp, callid, &call);
			break;
		case TCP_EV_DATA:
			tcp_ev_data(tcp, callid, &call);
			break;
		case TCP_EV_URG_DATA:
			tcp_ev_urg_data(tcp, callid, &call);
			break;
		case TCP_EV_NEW_CONN:
			tcp_ev_new_conn(tcp, callid, &call);
			break;
		default:
			async_answer_0(callid, ENOTSUP);
			break;
		}
	}
}

/** Fibril for handling incoming TCP connection in background */
static int tcp_conn_fibril(void *arg)
{
	tcp_in_conn_t *cinfo = (tcp_in_conn_t *)arg;

	cinfo->lst->lcb->new_conn(cinfo->lst, cinfo->conn);
	tcp_conn_destroy(cinfo->conn);

	return EOK;
}

/** @}
 */