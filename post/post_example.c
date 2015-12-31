/*
   This file is part of libmicrohttpd
   (C) 2011 Christian Grothoff (and other contributing authors)

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
/**
 * @file post_example.c
 * @brief example for processing POST requests using libmicrohttpd
 * @author Christian Grothoff
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <microhttpd.h>

/**
 * Invalid method page.
 */
#define METHOD_ERROR "<html><head><title>Illegal request</title></head><body>Go away.</body></html>"

/**
 * Invalid URL page.
 */
#define NOT_FOUND_ERROR "<html><head><title>Not found</title></head><body>Go away.</body></html>"

/**
 * Front page. (/)
 */
#define MAIN_PAGE "<html><head><title>Welcome</title></head><body><form action=\"/2\" method=\"post\">What is your name? <input type=\"text\" name=\"v1\" value=\"%s\" /><input type=\"submit\" value=\"Next\" /></body></html>"

/**
 * Second page. (/2)
 */
#define SECOND_PAGE "<html><head><title>Tell me more</title></head><body><a href=\"/\">previous</a> <form action=\"/S\" method=\"post\">%s, what is your job? <input type=\"text\" name=\"v2\" value=\"%s\" /><input type=\"submit\" value=\"Next\" /></body></html>"

/**
 * Second page (/S)
 */
#define SUBMIT_PAGE "<html><head><title>Ready to submit?</title></head><body><form action=\"/F\" method=\"post\"><a href=\"/2\">previous </a> <input type=\"hidden\" name=\"DONE\" value=\"yes\" /><input type=\"submit\" value=\"Submit\" /></body></html>"

/**
 * Last page.
 */
#define LAST_PAGE "<html><head><title>Thank you</title></head><body>Thank you.</body></html>"

/**
 * Name of our cookie.
 */
#define COOKIE_NAME "session"


/**
 * State we keep for each user/session/browser.
 */
struct Session
{
	/**
	 * We keep all sessions in a linked list.
	 */
	struct Session *next;

	/**
	 * Unique ID for this session. 
	 */
	char sid[33];

	/**
	 * Reference counter giving the number of connections
	 * currently using this session.
	 */
	unsigned int rc;

	/**
	 * Time when this session was last active.
	 */
	time_t start;

	/**
	 * String submitted via form.
	 */
	char value_1[64];

	/**
	 * Another value submitted via form.
	 */
	char value_2[64];

};


/**
 * Data kept per request.
 */
struct Request
{

	/**
	 * Associated session.
	 */
	struct Session *session;

	/**
	 * Post processor handling form data (IF this is
	 * a POST request).
	 */
	struct MHD_PostProcessor *pp;

	/**
	 * URL to serve in response to this POST (if this request 
	 * was a 'POST')
	 */
	const char *post_url;

};


/**
 * Linked list of all active sessions.  Yes, O(n) but a
 * hash table would be overkill for a simple example...
 */
static struct Session *sessions;




/**
 * Return the session handle for this connection, or 
 * create one if this is a new user.
 */
static struct Session *get_session (struct MHD_Connection *connection)
{
	printf("\n\n\n>>> get_session call!\n");
	struct Session *ret;
	const char *cookie;

	cookie = MHD_lookup_connection_value (connection,
			MHD_COOKIE_KIND,
			COOKIE_NAME);
	printf("get_session call, MHD_lookup_connection_value return cookie = %s\n", cookie);
	if (cookie != NULL)
	{
		printf("get_session call, cookie != NULL\n");
		/* find existing session */
		ret = sessions;
		while (NULL != ret)
		{
			printf("get_session call, in while(NULL != ret)\n");
			if (0 == strcmp (cookie, ret->sid))
			{
				printf("get_session call, because cookie(%s) == ret->sid(%s), we will break out from while(NULL != ret)\n", cookie, ret->sid);
				break;
			}
			printf("get_session call, because cookie(%s) != ret->sid(%s), we will set: ret = ret->next\n", cookie, ret->sid);
			ret = ret->next;
		}
		printf("get_session call, already break our from while(NULL != ret)\n");
		if (NULL != ret)
		{
			printf("get_session call, because NULL != ret, we will set: ret->rc++, then return ret\n");
			ret->rc++;
			return ret;
		}
		else
		{
			printf("get_session call, because NULL == ret, we are nothing to do \n");
		}
	}
	/* create fresh session */
	ret = calloc (1, sizeof (struct Session));
	if (NULL == ret)
	{						
		fprintf (stderr, "calloc error: %s\n", strerror (errno));
		return NULL; 
	}
	/* not a super-secure way to generate a random session ID,
	   but should do for a simple example... */
	snprintf (ret->sid,
			sizeof (ret->sid),
			"%X%X%X%X",
			(unsigned int) random (),
			(unsigned int) random (),
			(unsigned int) random (),
			(unsigned int) random ());
	printf("get_session call, ret->sid = %s\n", ret->sid);
	printf("get_session call, we will set: ret->rc++\n");
	ret->rc++;
	printf("get_session call, we will set: ret->start = time(NULL)\n");
	ret->start = time (NULL);
	printf("get_session call, we will set: ret->next = sessions\n");
	ret->next = sessions;
	printf("get_session call, we will set: sessions = ret\n");
	sessions = ret;
	printf("get_session call, we will return ret(%p)\n", ret);
	
	return ret;
}


/**
 * Type of handler that generates a reply.
 *
 * @param cls content for the page (handler-specific)
 * @param mime mime type to use
 * @param session session information
 * @param connection connection to process
 * @param MHD_YES on success, MHD_NO on failure
 */
typedef int (*PageHandler)(const void *cls,
		const char *mime,
		struct Session *session,
		struct MHD_Connection *connection);


/**
 * Entry we generate for each page served.
 */ 
struct Page
{
	/**
	 * Acceptable URL for this page.
	 */
	const char *url;

	/**
	 * Mime type to set for the page.
	 */
	const char *mime;

	/**
	 * Handler to call to generate response.
	 */
	PageHandler handler;

	/**
	 * Extra argument to handler.
	 */ 
	const void *handler_cls;
};


/**
 * Add header to response to set a session cookie.
 *
 * @param session session to use
 * @param response response to modify
 */ 
static void add_session_cookie (struct Session *session, struct MHD_Response *response)
{
	printf("\n\n\n>>> add_session_cookie call\n");
	char cstr[256];
	snprintf (cstr, sizeof (cstr), "%s=%s", COOKIE_NAME, session->sid);
	printf("add_session_cookie call, cstr = %s\n", cstr);
	printf("add_session_cookie call, we will invoke MHD_add_response_header\n");
	if (MHD_NO == MHD_add_response_header (response, MHD_HTTP_HEADER_SET_COOKIE, cstr))
	{
		printf("add_session_cookie call, MHD_add_response_header return value == MHD_NO, it's error\n");
		fprintf (stderr, "Failed to set session cookie header!\n");
	}
	printf("add_session_cookie call, we will return\n");
}


/**
 * Handler that returns a simple static HTTP page that
 * is passed in via 'cls'.
 *
 * @param cls a 'const char *' with the HTML webpage to return
 * @param mime mime type to use
 * @param session session handle 
 * @param connection connection to use
 */
static int serve_simple_form (const void *cls,
		const char *mime,
		struct Session *session,
		struct MHD_Connection *connection)
{
	printf("\n\n\n>>> serve_simple_form call\n");
	int ret;
	const char *form = cls;
	struct MHD_Response *response;
	printf("serve_simple_form call, form = %s, mime = %s, session = %p, connection = %p\n", form, mime, session, connection);

	/* return static form */
	printf("serve_simple_form call, we will invoke MHD_create_response_from_buffer\n");
	response = MHD_create_response_from_buffer (strlen (form),
			(void *) form,
			MHD_RESPMEM_PERSISTENT);
	printf("serve_simple_form call, we will invoke add_session_cookie\n");
	add_session_cookie (session, response);
	printf("serve_simple_form call, we will invoke MHD_add_response_header\n");
	MHD_add_response_header (response,
			MHD_HTTP_HEADER_CONTENT_ENCODING,
			mime);
	printf("serve_simple_form call, we will invoke MHD_queue_response\n");
	ret = MHD_queue_response (connection, 
			MHD_HTTP_OK, 
			response);
	printf("serve_simple_form call, we will invoke MHD_destroy_response\n");
	MHD_destroy_response (response);
	printf("serve_simple_form call, we will ret(%d)\n", ret);
	return ret;
}


/**
 * Handler that adds the 'v1' value to the given HTML code.
 *
 * @param cls a 'const char *' with the HTML webpage to return
 * @param mime mime type to use
 * @param session session handle 
 * @param connection connection to use
 */
static int fill_v1_form (const void *cls,
		const char *mime,
		struct Session *session,
		struct MHD_Connection *connection)
{
	printf("\n\n\n>>> fill_v1_form call\n");
	int ret;
	const char *form = cls;
	char *reply;
	struct MHD_Response *response;
	printf("fill_v1_form call, form = %s, mime = %s, session = %p, connection = %p\n", form, mime, session, connection);

	reply = malloc (strlen (form) + strlen (session->value_1) + 1);
	snprintf (reply,
			strlen (form) + strlen (session->value_1) + 1,
			form,
			session->value_1);
	printf("fill_v1_form call, reply = %s\n", reply);
	/* return static form */
	printf("fill_v1_form call, we will invoke MHD_create_response_from_buffer\n");
	response = MHD_create_response_from_buffer (strlen (reply),
			(void *) reply,
			MHD_RESPMEM_MUST_FREE);
	printf("fill_v1_form call, we will invoke add_session_cookie\n");
	add_session_cookie (session, response);
	printf("fill_v1_form call, we will invoke MHD_add_response_header\n");
	MHD_add_response_header (response,
			MHD_HTTP_HEADER_CONTENT_ENCODING,
			mime);
	printf("fill_v1_form call, we will invoke MHD_queue_response\n");
	ret = MHD_queue_response (connection, 
			MHD_HTTP_OK, 
			response);
	printf("fill_v1_form call, we will invoke MHD_destroy_response\n");
	MHD_destroy_response (response);
	printf("fill_v1_form call, we will return ret(%d)\n", ret);
	return ret;
}


/**
 * Handler that adds the 'v1' and 'v2' values to the given HTML code.
 *
 * @param cls a 'const char *' with the HTML webpage to return
 * @param mime mime type to use
 * @param session session handle 
 * @param connection connection to use
 */
static int fill_v1_v2_form (const void *cls,
		const char *mime,
		struct Session *session,
		struct MHD_Connection *connection)
{
	printf("\n\n\n>>> fill_v1_v2_form call\n");
	int ret;
	const char *form = cls;
	char *reply;
	struct MHD_Response *response;
	printf("fill_v1_v2_form call, form = %s, mime = %s, session = %p, connection = %p\n", form, mime, session, connection);

	reply = malloc (strlen (form) + strlen (session->value_1) + strlen (session->value_2) + 1);
	snprintf (reply,
			strlen (form) + strlen (session->value_1) + strlen (session->value_2) + 1,
			form,
			session->value_1);
	printf("fill_v1_v2_form call, reply = %s\n", reply);
	/* return static form */
	printf("fill_v1_v2_form call, we will invoke MHD_create_response_from_buffer\n");
	response = MHD_create_response_from_buffer (strlen (reply),
			(void *) reply,
			MHD_RESPMEM_MUST_FREE);
	printf("fill_v1_v2_form call, we will invoke add_session_cookie\n");
	add_session_cookie (session, response);
	printf("fill_v1_v2_form call, we will invoke MHD_add_response_header\n");
	MHD_add_response_header (response,
			MHD_HTTP_HEADER_CONTENT_ENCODING,
			mime);
	printf("fill_v1_v2_form call, we will invoke MHD_queue_response\n");
	ret = MHD_queue_response (connection, 
			MHD_HTTP_OK, 
			response);
	printf("fill_v1_v2_form call, we will invoke MHD_destroy_response\n");
	MHD_destroy_response (response);
	printf("fill_v1_v2_form call, we will return ret(%d)\n", ret);
	return ret;
}


/**
 * Handler used to generate a 404 reply.
 *
 * @param cls a 'const char *' with the HTML webpage to return
 * @param mime mime type to use
 * @param session session handle 
 * @param connection connection to use
 */
static int not_found_page (const void *cls,
		const char *mime,
		struct Session *session,
		struct MHD_Connection *connection)
{
	printf("\n\n\n>>> not_found_page call\n");
	int ret;
	struct MHD_Response *response;
	printf("not_found_page call, (char *)cls = %s, mime = %s, session = %p, connection = %p\n", (char *)cls, mime, session, connection);

	/* unsupported HTTP method */
	printf("not_found_page call, we will invoke MHD_create_response_from_buffer\n");
	response = MHD_create_response_from_buffer (strlen (NOT_FOUND_ERROR),
			(void *) NOT_FOUND_ERROR,
			MHD_RESPMEM_PERSISTENT);
	printf("not_found_page call, we will invoke MHD_queue_response\n");
	ret = MHD_queue_response (connection, 
			MHD_HTTP_NOT_FOUND, 
			response);
	printf("not_found_page call, we will invoke MHD_add_response_header\n");
	MHD_add_response_header (response,
			MHD_HTTP_HEADER_CONTENT_ENCODING,
			mime);
	printf("not_found_page call, we will invoke MHD_destroy_response\n");
	MHD_destroy_response (response);
	printf("not_found_page call, we will return ret(%d)\n", ret);
	return ret;
}


/**
 * List of all pages served by this HTTP server.
 */
static struct Page pages[] = 
{
	{ "/", "text/html",  &fill_v1_form, MAIN_PAGE },
	{ "/2", "text/html", &fill_v1_v2_form, SECOND_PAGE },
	{ "/S", "text/html", &serve_simple_form, SUBMIT_PAGE },
	{ "/F", "text/html", &serve_simple_form, LAST_PAGE },
	{ NULL, NULL, &not_found_page, NULL } /* 404 */
};



/**
 * Iterator over key-value pairs where the value
 * maybe made available in increments and/or may
 * not be zero-terminated.  Used for processing
 * POST data.
 *
 * @param cls user-specified closure
 * @param kind type of the value
 * @param key 0-terminated key for the value
 * @param filename name of the uploaded file, NULL if not known
 * @param content_type mime-type of the data, NULL if not known
 * @param transfer_encoding encoding of the data, NULL if not known
 * @param data pointer to size bytes of data at the
 *              specified offset
 * @param off offset of data in the overall value
 * @param size number of bytes in data available
 * @return MHD_YES to continue iterating,
 *         MHD_NO to abort the iteration
 */
static int post_iterator (void *cls,
		enum MHD_ValueKind kind,
		const char *key,
		const char *filename,
		const char *content_type,
		const char *transfer_encoding,
		const char *data, uint64_t off, size_t size)
{
	printf("\n\n\n>>> post_iterator call\n");
	struct Request *request = cls;
	struct Session *session = request->session;
	printf("post_iterator call, key = %s, filename = %s, content_type = %s, transfer_encoding = %s, data = %s, off = %llu, size = %ld\n", key, filename, content_type, transfer_encoding, data, off, size);

	if (0 == strcmp ("DONE", key))
	{
		printf("post_iterator call, key = DONE, we will do something\n");
		fprintf (stdout,
				"Session `%s' submitted `%s', `%s'\n",
				session->sid,
				session->value_1,
				session->value_2);
		printf("post_iterator call, we will return MHD_YES(%d)", MHD_YES);
		return MHD_YES;
	}
	else
	{
		printf("post_iterator call, key != DONE, we will not to do anything\n");
	}

	if (0 == strcmp ("v1", key))
	{
		printf("post_iterator call, key == v1, we will do something\n");
		if (size + off > sizeof(session->value_1))
			size = sizeof (session->value_1) - off;
		memcpy (&session->value_1[off], data, size);
		printf("&session->value_1[off] = %s, off = %llu, data = %s, size = %ld", &session->value_1[off], off, data, size);
		if (size + off < sizeof (session->value_1))
			session->value_1[size+off] = '\0';
		printf("post_iterator call, we will return MHD_YES(%d)\n", MHD_YES);
		return MHD_YES;
	}
	else
	{
		printf("post_iterator call, key != v1, we will not to do anything\n");
	}

	if (0 == strcmp ("v2", key))
	{
		printf("post_iterator call, key == v2, we will do something\n");
		if (size + off > sizeof(session->value_2))
			size = sizeof (session->value_2) - off;
		memcpy (&session->value_2[off], data, size);
		printf("&session->value_2[off] = %s, off = %llu, data = %s, size = %ld", &session->value_2[off], off, data, size);
		if (size + off < sizeof (session->value_2))
			session->value_2[size+off] = '\0';
		printf("post_iterator call, we will return MHD_YES(%d)\n", MHD_YES);
		return MHD_YES;
	}
	else
	{
		printf("post_iterator call, key != v2, we will not to do anything\n");
	}
	fprintf (stderr, "Unsupported form value `%s'\n", key);
	printf("post_iterator call, we will return MHD_YES(%d)\n", MHD_YES);
	return MHD_YES;
}


/**
 * Main MHD callback for handling requests.
 *
 *
 * @param cls argument given together with the function
 *        pointer when the handler was registered with MHD
 * @param url the requested url
 * @param method the HTTP method used ("GET", "PUT", etc.)
 * @param version the HTTP version string (i.e. "HTTP/1.1")
 * @param upload_data the data being uploaded (excluding HEADERS,
 *        for a POST that fits into memory and that is encoded
 *        with a supported encoding, the POST data will NOT be
 *        given in upload_data and is instead available as
 *        part of MHD_get_connection_values; very large POST
 *        data *will* be made available incrementally in
 *        upload_data)
 * @param upload_data_size set initially to the size of the
 *        upload_data provided; the method must update this
 *        value to the number of bytes NOT processed;
 * @param con_cls pointer that the callback can set to some
 *        address and that will be preserved by MHD for future
 *        calls for this request; since the access handler may
 *        be called many times (i.e., for a PUT/POST operation
 *        with plenty of upload data) this allows the application
 *        to easily associate some request-specific state.
 *        If necessary, this state can be cleaned up in the
 *        global "MHD_RequestCompleted" callback (which
 *        can be set with the MHD_OPTION_NOTIFY_COMPLETED).
 *        Initially, <tt>*con_cls</tt> will be NULL.
 * @return MHS_YES if the connection was handled successfully,
 *         MHS_NO if the socket must be closed due to a serios
 *         error while handling the request
 */
static int create_response (void *cls,
		struct MHD_Connection *connection,
		const char *url,
		const char *method,
		const char *version,
		const char *upload_data, 
		size_t *upload_data_size,
		void **ptr)
{
	printf("\n\n\n>>> create_response call\n");

	printf("create_response call, cls = %p, (char *cls) = %s\n", cls, (char *)cls);
        printf("create_response call, url = %s\n", url);
        printf("create_response call, method = %s\n", method);
        printf("create_response call, version = %s\n", version);
        printf("create_response call, upload_data = %s\n", upload_data);
        if (upload_data_size) printf("create_response call, *upload_data_size = %ld\n", *upload_data_size);
        printf("create_response call, ptr = %p\n", ptr);


	struct MHD_Response *response;
	struct Request *request;
	struct Session *session;
	int ret;
	unsigned int i;

	request = *ptr;
	if (NULL == request)
	{
		printf("create_response call, request = NULL, we will do something\n");
		request = calloc (1, sizeof (struct Request));
		if (NULL == request)
		{
			fprintf (stderr, "calloc error: %s\n", strerror (errno));
			return MHD_NO;
		}
		*ptr = request;
		
		printf("create_response call, MHD_HTTP_METHOD_POST = %s\n", MHD_HTTP_METHOD_POST);
		if (0 == strcmp (method, MHD_HTTP_METHOD_POST))
		{
			printf("create_response call, <0 == strcmp (method, MHD_HTTP_METHOD_POST)> is true, we will invoke MHD_create_post_processor\n");
			request->pp = MHD_create_post_processor (connection, 1024, &post_iterator, request);
			if (NULL == request->pp)
			{
				fprintf (stderr, "Failed to setup post processor for `%s'\n", url);
				return MHD_NO; /* internal error */
			}
		}
		printf("create_response call, we will return MHD_YES(%d)\n", MHD_YES);
		return MHD_YES;
	}
	else
	{
		printf("create_response call, request != NULL, we will not to do anything\n");
	}

	if (NULL == request->session)
	{
		printf("create_response call, request->session == NULL, we will do something\n");
		request->session = get_session (connection);
		if (NULL == request->session)
		{
			fprintf (stderr, "Failed to setup session for `%s'\n", url);
			return MHD_NO; /* internal error */
		}
	}
	else
	{
		printf("create_response call, request->session != NULL, we will not to do anything\n");
	}

	session = request->session;
	session->start = time (NULL);
	if (0 == strcmp (method, MHD_HTTP_METHOD_POST))
	{      
		printf("create_response call, <0 == strcmp (method, MHD_HTTP_METHOD_POST)> is true, we will do something\n");

		/* evaluate POST data */
		MHD_post_process (request->pp,
				upload_data,
				*upload_data_size);
		if (0 != *upload_data_size)
		{
			*upload_data_size = 0;
			return MHD_YES;
		}
		/* done with POST data, serve response */
		MHD_destroy_post_processor (request->pp);
		request->pp = NULL;
		method = MHD_HTTP_METHOD_GET; /* fake 'GET' */
		if (NULL != request->post_url)
			url = request->post_url;
	}
	else
	{
		printf("create_response call, <0 == strcmp (method, MHD_HTTP_METHOD_POST)> is false, we will not to do anything\n");
	}
	
	printf("create_response call, MHD_HTTP_METHOD_GET = %s, MHD_HTTP_METHOD_HEAD = %s\n", MHD_HTTP_METHOD_GET, MHD_HTTP_METHOD_HEAD);
	if ( (0 == strcmp (method, MHD_HTTP_METHOD_GET)) || (0 == strcmp (method, MHD_HTTP_METHOD_HEAD)) )
	{
		printf("create_response call, method(%s) == MHD_HTTP_METHOD_GET or method(%s) == MHD_HTTP_METHOD_HEAD, we will do something\n", method, method);
		/* find out which page to serve */
		i=0;
		while ( (pages[i].url != NULL) && (0 != strcmp (pages[i].url, url)) )
		{
			printf("create_response call, pages[%d].url = %s, url = %s, pages[%d].mime = %s\n", i, pages[i].url, url, i, pages[i].mime);
			i++;
		}
		printf("create_response call, pages[%d].url = %s, url = %s, pages[%d].mime = %s\n", i, pages[i].url, url, i, pages[i].mime);
		printf("create_response call, we will invoke pages[%d].handler\n", i);
		ret = pages[i].handler (pages[i].handler_cls, pages[i].mime, session, connection);
		if (ret != MHD_YES)
			fprintf (stderr, "Failed to create page for `%s'\n", url);
		printf("create_response call, we will return ret(%d)\n", ret);
		return ret;
	}
	else
	{
		printf("create_response call, <(0 == strcmp (method, MHD_HTTP_METHOD_GET)) || (0 == strcmp (method, MHD_HTTP_METHOD_HEAD))> is false, we will not to do anything\n");
	}
	printf("create_response call, will invoke MHD_create_from_buffer\n");
	/* unsupported HTTP method */
	response = MHD_create_response_from_buffer (strlen (METHOD_ERROR),
			(void *) METHOD_ERROR,
			MHD_RESPMEM_PERSISTENT);
	printf("create_response call, will invoke MHD_queue_response\n");
	ret = MHD_queue_response (connection, 
			MHD_HTTP_METHOD_NOT_ACCEPTABLE, 
			response);
	printf("create_response call, will invoke MHD_destroy_response\n");
	MHD_destroy_response (response);
	printf("create_response call, will return\n");
	return ret;
}


/**
 * Callback called upon completion of a request.
 * Decrements session reference counter.
 *
 * @param cls not used
 * @param connection connection that completed
 * @param con_cls session handle
 * @param toe status code
 */
static void request_completed_callback (void *cls,
		struct MHD_Connection *connection,
		void **con_cls,
		enum MHD_RequestTerminationCode toe)
{
	printf("\n\n\n>>> request_completed_callback call\n");
	struct Request *request = *con_cls;

	if (NULL == request)
	{
		printf("request_completed_callback call, because NULL == request, we will return\n");
		return;
	}
	if (NULL != request->session)
	{
		printf("request_completed_callback call, because NULL != request->session, we will set request->session->rc--\n");
		request->session->rc--;
	}
	if (NULL != request->pp)
	{
		printf("request_completed_callback call, because NULL != request->pp, we will MHD_destroy_post_processor\n");
		MHD_destroy_post_processor (request->pp);
	}
	printf("request_completed_callback call, we will free(request)\n");
	free (request);
	printf("request_completed_callback call, we will return\n");
	return;
}


/**
 * Clean up handles of sessions that have been idle for
 * too long.
 */
static void expire_sessions ()
{
	printf("\n\n\n>>> expire_sessions call!\n");
	struct Session *pos;
	struct Session *prev;
	struct Session *next;
	time_t now;

	now = time (NULL);
	printf("expire_sessions call, now = %ld\n", now);
	prev = NULL;
	pos = sessions;
	printf("expire_sessions call, pos = %p\n", pos);
	while (NULL != pos)
	{
		printf("expire_sessions call, in while()\n");
		next = pos->next;
		if (now - pos->start > 60 * 60)
		{
			printf("expire_sessions call, now - pos->start = %ld > 60*60, we will free current session:%p\n", now - pos->start, pos);
			/* expire sessions after 1h */
			if (NULL == prev)
				sessions = pos->next;
			else
				prev->next = next;
			free (pos);
		}
		else
		{
			printf("expire_sessions call, now - pos->start = %ld <= 60*60, we will check next session\n", now - pos->start);
			prev = pos;
		}
		pos = next;
	}
	printf("expire_sessions call, we will return\n");
}


/**
 * Call with the port number as the only argument.
 * Never terminates (other than by signals, such as CTRL-C).
 */
int main (int argc, char *const *argv)
{
	struct MHD_Daemon *d;
	struct timeval tv;
	struct timeval *tvp;
	fd_set rs;
	fd_set ws;
	fd_set es;
	int max;
	unsigned MHD_LONG_LONG mhd_timeout;

	if (argc != 2)
	{
		printf ("%s PORT\n", argv[0]);
		return 1;
	}
	/* initialize PRNG */
	srandom ((unsigned int) time (NULL));
	d = MHD_start_daemon (MHD_USE_DEBUG,
			atoi (argv[1]),
			NULL, NULL, 
			&create_response, NULL, 
			MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) 15,
			MHD_OPTION_NOTIFY_COMPLETED, &request_completed_callback, NULL,
			MHD_OPTION_END);
	if (NULL == d)
	{
		printf("main call, MHD_start_daemon return NULL, we will return 1\n");
		return 1;
	}
	while (1)
	{
		printf("main call, now in while(1)\n");
		expire_sessions ();
		max = 0;
		FD_ZERO (&rs);
		FD_ZERO (&ws);
		FD_ZERO (&es);
		printf("main call, 1 >>> max = %d\n", max);
		if (MHD_YES != MHD_get_fdset (d, &rs, &ws, &es, &max))
		{
			printf("main call, 2 >>> max = %d\n", max);
			printf("main call, MHD_get_fdset return != MHD_YES, we will break out while(1)\n");
			break; /* fatal internal error */
		}
		printf("main call, 1 >>> mhd_timeout = %llu\n", mhd_timeout);
		if (MHD_get_timeout (d, &mhd_timeout) == MHD_YES)
		{
			printf("main call, 2 >>> mhd_timeout = %llu\n", mhd_timeout);
			printf("main call, MHD_get_timeout return = MHD_YES\n");
			tv.tv_sec = mhd_timeout / 1000;
			tv.tv_usec = (mhd_timeout - (tv.tv_sec * 1000)) * 1000;
			printf("main call, tv.tv_sec = %ld\n", tv.tv_sec);
			printf("main call, tv.tv_usec = %ld\n", tv.tv_usec);
			tvp = &tv;
		}
		else
		{
			printf("main call, MHD_get_timeout return != MHD_YES, we will set tvp = NULL\n");
			tvp = NULL;
		}
		printf("main call, we will invoke select\n");
		select (max + 1, &rs, &ws, &es, tvp);
		printf("main call, we will invoke MHD_run\n");
		MHD_run (d);
	}
	printf("main call, we will invoke MHD_stop_daemon\n");
	MHD_stop_daemon (d);
	return 0;
}
