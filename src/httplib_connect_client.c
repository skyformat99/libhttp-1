/* 
 * Copyright (c) 2016 Lammert Bies
 * Copyright (c) 2013-2016 the Civetweb developers
 * Copyright (c) 2004-2013 Sergey Lyubka
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "httplib_main.h"
#include "httplib_memory.h"
#include "httplib_pthread.h"
#include "httplib_ssl.h"
#include "httplib_string.h"

static struct mg_connection *	mg_connect_client_impl( const struct mg_client_options *client_options, int use_ssl, char *ebuf, size_t ebuf_len );



/*
 * struct mg_connection *mg_connect_client_secure( const struct mg_client_options *client options, char *error buffer, size_t error_buffer_size );
 *
 * The function mg_connect_client_secure() creates a secure connection as a
 * client to a remote server and returns a pointer to the connection
 * information, or NULL if an error occured.
 */

CIVETWEB_API struct mg_connection *mg_connect_client_secure( const struct mg_client_options *client_options, char *error_buffer, size_t error_buffer_size ) {

	return mg_connect_client_impl( client_options, 1, error_buffer, error_buffer_size );

}  /* mg_connect_client_secure */



/*
 * struct mg_connection *mg_connect_client( const char *host, int port, int use_ssl, char *error_buffer, size_t error_buffer_size );
 *
 * The function mg_connect_client() connects to a remote server as a client
 * with the options of the connection provided as parameters.
 */

struct mg_connection * mg_connect_client( const char *host, int port, int use_ssl, char *error_buffer, size_t error_buffer_size ) {

	struct mg_client_options opts;

	memset( &opts, 0, sizeof(opts) );
	opts.host = host;
	opts.port = port;

	return mg_connect_client_impl( &opts, use_ssl, error_buffer, error_buffer_size );

}  /* mg_connect_client */



/*
 * static struct mg_connection *mg_connect_client_impl( const struct mg_client_options *client_options, int use_ssl, char *ebuf, size_t ebuf_len );
 *
 * The function mg_connect_client_impl() is the background function doing the
 * heavy lifting to make connections as a client to remote servers.
 */

static struct mg_connection *mg_connect_client_impl( const struct mg_client_options *client_options, int use_ssl, char *ebuf, size_t ebuf_len ) {

	static struct mg_context fake_ctx;
	struct mg_connection *conn = NULL;
	SOCKET sock;
	union usa sa;

	if (!XX_httplib_connect_socket(&fake_ctx, client_options->host, client_options->port, use_ssl, ebuf, ebuf_len, &sock, &sa)) {
		;
	} else if ((conn = (struct mg_connection *)
	                XX_httplib_calloc(1, sizeof(*conn) + MAX_REQUEST_SIZE)) == NULL) {
		XX_httplib_snprintf(NULL, NULL, ebuf, ebuf_len, "calloc(): %s", strerror(ERRNO));
		closesocket(sock);
#ifndef NO_SSL
	} else if (use_ssl && (conn->client_ssl_ctx = SSL_CTX_new(SSLv23_client_method())) == NULL) {

		XX_httplib_snprintf(NULL, NULL, ebuf, ebuf_len, "SSL_CTX_new error");
		closesocket(sock);
		XX_httplib_free(conn);
		conn = NULL;
#endif /* NO_SSL */

	} else {

#ifdef USE_IPV6
		socklen_t len = (sa.sa.sa_family == AF_INET) ? sizeof(conn->client.rsa.sin) : sizeof(conn->client.rsa.sin6);
		struct sockaddr *psa = (sa.sa.sa_family == AF_INET) ? (struct sockaddr *)&(conn->client.rsa.sin) : (struct sockaddr *)&(conn->client.rsa.sin6);
#else
		socklen_t len = sizeof(conn->client.rsa.sin);
		struct sockaddr *psa = (struct sockaddr *)&(conn->client.rsa.sin);
#endif

		conn->buf_size    = MAX_REQUEST_SIZE;
		conn->buf         = (char *)(conn + 1);
		conn->ctx         = &fake_ctx;
		conn->client.sock = sock;
		conn->client.lsa  = sa;

		if (getsockname(sock, psa, &len) != 0) mg_cry(conn, "%s: getsockname() failed: %s", __func__, strerror(ERRNO));

		conn->client.is_ssl = use_ssl ? 1 : 0;
		pthread_mutex_init(&conn->mutex, &XX_httplib_pthread_mutex_attr);

#ifndef NO_SSL
		if (use_ssl) {

			fake_ctx.ssl_ctx = conn->client_ssl_ctx;

			/* TODO: Check ssl_verify_peer and ssl_ca_path here.
			 * SSL_CTX_set_verify call is needed to switch off server
			 * certificate checking, which is off by default in OpenSSL and
			 * on in yaSSL. */
			/* TODO: SSL_CTX_set_verify(conn->client_ssl_ctx,
			 * SSL_VERIFY_PEER, verify_ssl_server); */

			if (client_options->client_cert) {
				if (!XX_httplib_ssl_use_pem_file(&fake_ctx, client_options->client_cert)) {
					XX_httplib_snprintf(NULL, NULL, ebuf, ebuf_len, "Can not use SSL client certificate");
					SSL_CTX_free(conn->client_ssl_ctx);
					closesocket(sock);
					XX_httplib_free(conn);
					conn = NULL;
				}
			}

			if (client_options->server_cert) {
				SSL_CTX_load_verify_locations(conn->client_ssl_ctx, client_options->server_cert, NULL);
				SSL_CTX_set_verify(conn->client_ssl_ctx, SSL_VERIFY_PEER, NULL);
			} else {
				SSL_CTX_set_verify(conn->client_ssl_ctx, SSL_VERIFY_NONE, NULL);
			}

			if (!XX_httplib_sslize(conn, conn->client_ssl_ctx, SSL_connect)) {
				XX_httplib_snprintf(NULL, NULL, ebuf, ebuf_len, "SSL connection error");
				SSL_CTX_free(conn->client_ssl_ctx);
				closesocket(sock);
				XX_httplib_free(conn);
				conn = NULL;
			}
		}
#endif
	}

	return conn;

}  /* mg_connect_client_impl */
