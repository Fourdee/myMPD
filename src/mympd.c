/* myMPD
   (c) 2018 Juergen Mang <mail@jcgames.de>
   This project's homepage is: https://github.com/jcorporation/mympd
   
   myMPD ist fork of:
   
   ympd
   (c) 2013-2014 Andrew Karpow <andy@ndyk.de>
   This project's homepage is: http://www.ympd.org
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <pwd.h>
#include <grp.h>

#include "../dist/src/mongoose/mongoose.h"
#include "../dist/src/frozen/frozen.h"
#include "../dist/src/inih/ini.h"
#include "mpd_client.h"
#include "config.h"

static sig_atomic_t s_signal_received = 0;
static struct mg_serve_http_opts s_http_server_opts;


static void signal_handler(int sig_num) {
    signal(sig_num, signal_handler);  // Reinstantiate signal handler
    s_signal_received = sig_num;
}

static void handle_api(struct mg_connection *nc, struct http_message *hm) {
    if (!is_websocket(nc))
        mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Type: application/json\r\n\r\n");

    char buf[1000] = {0};
    int len = sizeof(buf) - 1 < hm->body.len ? sizeof(buf) - 1 : hm->body.len;
    memcpy(buf, hm->body.p, len);
    struct mg_str d = {buf, len};
    callback_mympd(nc, d);

    if (!is_websocket(nc))
        mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
    switch(ev) {
        case MG_EV_WEBSOCKET_HANDSHAKE_REQUEST: {
            struct http_message *hm = (struct http_message *) ev_data;
            #ifdef DEBUG
            fprintf(stderr, "New websocket request: %.*s\n", hm->uri.len, hm->uri.p);
            #endif
            if (mg_vcmp(&hm->uri, "/ws") != 0) {
                printf("Websocket request not to /ws, closing connection\n");
                mg_printf(nc, "%s", "HTTP/1.1 403 FORBIDDEN\r\n\r\n");
                nc->flags |= MG_F_SEND_AND_CLOSE;
            }
            break;
        }
        case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
             #ifdef DEBUG
             fprintf(stderr, "New Websocket connection established\n");
             #endif
             struct mg_str d = mg_mk_str("{\"cmd\": \"MPD_API_WELCOME\"}");
             callback_mympd(nc, d);
             break;
        }
        case MG_EV_HTTP_REQUEST: {
            struct http_message *hm = (struct http_message *) ev_data;
            #ifdef DEBUG
            fprintf(stderr, "HTTP request: %.*s\n", hm->uri.len, hm->uri.p);
            #endif
            if (mg_vcmp(&hm->uri, "/api") == 0)
                handle_api(nc, hm);
            else
                mg_serve_http(nc, hm, s_http_server_opts);
            break;
        }
        case MG_EV_CLOSE: {
            if (is_websocket(nc)) {
              #ifdef DEBUG
              fprintf(stderr, "Websocket connection closed\n");
              #endif
            }
            else {
              #ifdef DEBUG
              fprintf(stderr,"HTTP connection closed\n");
              #endif
            }
            break;
        }        
    }
}

static void ev_handler_http(struct mg_connection *nc_http, int ev, void *ev_data) {
    switch(ev) {
        case MG_EV_HTTP_REQUEST: {
            struct http_message *hm = (struct http_message *) ev_data;
            struct mg_str *host_hdr = mg_get_http_header(hm, "Host");
            char s_redirect[250];
            snprintf(s_redirect, 250, "https://%.*s:%s/", host_hdr->len, host_hdr->p, config.sslport);
            printf("Redirecting to %s\n", s_redirect);
            mg_http_send_redirect(nc_http, 301, mg_mk_str(s_redirect), mg_mk_str(NULL));
            break;
        }
    }
}

static int inihandler(void* user, const char* section, const char* name, const char* value) {
    t_config* p_config = (t_config*)user;
    char *crap;

    #define MATCH(n) strcmp(name, n) == 0

    if (MATCH("mpdhost"))
        p_config->mpdhost = strdup(value);
    else if (MATCH("mpdhost"))
        p_config->mpdhost = strdup(value);
    else if (MATCH("mpdport"))
        p_config->mpdport = strtol(value, &crap, 10);
    else if (MATCH("mpdhost"))
        p_config->mpdhost = strdup(value);
    else if (MATCH("mpdpass"))
        p_config->mpdpass = strdup(value);
    else if (MATCH("webport"))
        p_config->webport = strdup(value);
    else if (MATCH("ssl"))
        if (strcmp(value, "true") == 0)
            p_config->ssl = true;
        else
            p_config->ssl = false;
    else if (MATCH("sslcert"))
        p_config->sslcert = strdup(value);
    else if (MATCH("sslkey"))
        p_config->sslkey = strdup(value);
    else if (MATCH("user"))
        p_config->user = strdup(value);
    else if (MATCH("streamport"))
        p_config->streamport = strtol(value, &crap, 10);
    else if (MATCH("coverimage"))
        p_config->coverimage = strdup(value);
    else if (MATCH("statefile"))
        p_config->statefile = strdup(value);
    else if (MATCH("stickers"))
        if (strcmp(value, "true") == 0)
            p_config->stickers = true;
        else
            p_config->stickers = false;
    else if (MATCH("mixramp"))
        if (strcmp(value, "true") == 0)
            p_config->mixramp = true;
        else
            p_config->mixramp = false;
    else if (MATCH("taglist"))
        p_config->taglist = strdup(value);            
    else
        return 0;  /* unknown section/name, error */

    return 1;
}

int main(int argc, char **argv) {
    struct mg_mgr mgr;
    struct mg_connection *nc;
    struct mg_connection *nc_http;
    struct mg_bind_opts bind_opts;
    const char *err;
    
    //defaults
    config.mpdhost = "127.0.0.1";
    config.mpdport = 6600;
    config.mpdpass = NULL;
    config.webport = "80";
    config.ssl = true;
    config.sslport = "443";
    config.sslcert = "/etc/mympd/ssl/server.pem";
    config.sslkey = "/etc/mympd/ssl/server.key";
    config.user = "nobody";
    config.streamport = 8000;
    config.coverimage = "folder.jpg";
    config.statefile = "/var/lib/mympd/mympd.state";
    config.stickers = true;
    config.mixramp = true;
    config.taglist = "Artist,Album,AlbumArtist,Title,Track,Genre,Date,Composer,Performer";
    
    mpd.timeout = 3000;
    mpd.last_update_sticker_song_id = -1;
    mpd.last_song_id = -1;
    
    if (argc == 2) {
        printf("Parsing config file: %s\n", argv[1]);
        if (ini_parse(argv[1], inihandler, &config) < 0) {
            printf("Can't load config file \"%s\"\n", argv[1]);
            return EXIT_FAILURE;
        }
    } 
    else {
        printf("myMPD  %s\n"
            "Copyright (C) 2018 Juergen Mang <mail@jcgames.de>\n"
            "https://github.com/jcorporation/myMPD\n"
            "Built " __DATE__ " "__TIME__"\n\n"
            "Usage: %s /path/to/mympd.conf\n",
            MYMPD_VERSION,
            argv[0]
        );
        return EXIT_FAILURE;    
    }

    printf("Starting myMPD %s\n", MYMPD_VERSION);

    DIR* dir = opendir(SRC_PATH);
    if (dir) {
        printf("Document root: %s\n", SRC_PATH);
        closedir(dir);
    }
    else {
        printf("Document root \"%s\" don't exists\n", SRC_PATH);
        return EXIT_FAILURE;
    }

    if (access( config.statefile, F_OK ) != -1 ) {
        char *content = json_fread(config.statefile);
        int je = json_scanf(content, strlen(content), "{notificationWeb: %B, notificationPage: %B, jukeboxMode: %B, jukeboxPlaylist: %Q}", 
            &mympd_state.notificationWeb, 
            &mympd_state.notificationPage,
            &mympd_state.jukeboxMode,
            &mympd_state.jukeboxPlaylist);
        if (je != 4) {
            mympd_state.notificationWeb = false;
            mympd_state.notificationPage = true;
            mympd_state.jukeboxMode = false;
            mympd_state.jukeboxPlaylist = "Database";
        }
    } else {
        mympd_state.notificationWeb = false;
        mympd_state.notificationPage = true;
        mympd_state.jukeboxMode = false;
        mympd_state.jukeboxPlaylist = "Database";
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    
    mg_mgr_init(&mgr, NULL);

    if (config.ssl == true) {
        nc_http = mg_bind(&mgr, config.webport, ev_handler_http);
        if (nc_http == NULL) {
            printf("Error listening on port %s\n", config.webport);
            return EXIT_FAILURE;
        }
        memset(&bind_opts, 0, sizeof(bind_opts));
        bind_opts.ssl_cert = config.sslcert;
        bind_opts.ssl_key = config.sslkey;
        bind_opts.error_string = &err;
        
        nc = mg_bind_opt(&mgr, config.sslport, ev_handler, bind_opts);
        if (nc == NULL) {
            printf("Error listening on port %s: %s\n", config.sslport, err);
            mg_mgr_free(&mgr);
            return EXIT_FAILURE;
        }
    }
    else {
        nc = mg_bind(&mgr, config.webport, ev_handler);
        if (nc == NULL) {
            printf("Error listening on port %s\n", config.webport);
            mg_mgr_free(&mgr);
            return EXIT_FAILURE;
        }
    }

    if (config.user != NULL) {
        printf("Droping privileges to %s\n", config.user);
        struct passwd *pw;
        if ((pw = getpwnam(config.user)) == NULL) {
            printf("Unknown user\n");
            mg_mgr_free(&mgr);
            return EXIT_FAILURE;
        } else if (setgroups(0, NULL) != 0) { 
            printf("setgroups() failed\n");
            mg_mgr_free(&mgr);
            return EXIT_FAILURE;        
        } else if (setgid(pw->pw_gid) != 0) {
            printf("setgid() failed\n");
            mg_mgr_free(&mgr);
            return EXIT_FAILURE;
        } else if (setuid(pw->pw_uid) != 0) {
            printf("setuid() failed\n");
            mg_mgr_free(&mgr);
            return EXIT_FAILURE;
        }
    }
    
    if (getuid() == 0) {
        printf("myMPD should not be run with root privileges\n");
        mg_mgr_free(&mgr);
        return EXIT_FAILURE;
    }
    
    if (config.ssl == true)
        mg_set_protocol_http_websocket(nc_http);
    
    mg_set_protocol_http_websocket(nc);
    s_http_server_opts.document_root = SRC_PATH;
    s_http_server_opts.enable_directory_listing = "no";

    printf("Listening on http port %s\n", config.webport);
    if (config.ssl == true)
        printf("Listening on ssl port %s\n", config.sslport);

    while (s_signal_received == 0) {
        mg_mgr_poll(&mgr, 100);
        mympd_idle(&mgr, 0);
    }
    mg_mgr_free(&mgr);
    mympd_disconnect();
    return EXIT_SUCCESS;
}
