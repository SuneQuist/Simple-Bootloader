/* Tiny64 browser: one live Lexbor document, URL-only history, asynchronous
 * HTTP and certificate-verified HTTPS over the shared NIC receive queue. */
#include <stddef.h>
#include <stdint.h>
#include <lexbor/html/html.h>
#include "../console/console.h"
#include "../memory/heap.h"
#include "../networking/http.h"
#include "../networking/tls_probe_transport.h"
#include "../networking/net.h"
#include "../bin/basic.h"
#include "../tls/tls_probe.h"
#include "../tls/tls_clock.h"
#include "renderer.h"
#include "navigation.h"

#define NAV_URL_CAP 256
#define NAV_PAGE_CAP 65536
#define NAV_HISTORY_MAX 12

static char pending_url[NAV_URL_CAP];
static char request_url[NAV_URL_CAP];
static char current_url[NAV_URL_CAP];
static char history[NAV_HISTORY_MAX][NAV_URL_CAP];
static int history_count, history_index = -1;
static int pending, fetching, request_secure, navigation_mode, redirect_count;
static int document_valid;
static char *active_html;

static size_t str_len(const char *s) { size_t i=0; while(s[i]) i++; return i; }
static int copy_string(char *dst, size_t cap, const char *src) {
    size_t n=str_len(src); if(n>=cap) return 0;
    for(size_t i=0;i<=n;i++) dst[i]=src[i];
    return 1;
}
static int same(const char *a, const char *b) {
    size_t i=0; while(a[i] && a[i]==b[i]) i++;
    return a[i]==0 && b[i]==0;
}
static int prefix(const char *s, const char *p) {
    while(*p) if(*s++!=*p++) return 0;
    return 1;
}
static unsigned scheme_len(const char *url) {
    if(prefix(url,"https://")) return 8;
    if(prefix(url,"http://")) return 7;
    return 0;
}

/* Resolve absolute, protocol-relative, root-relative, path-relative, and
 * query-only links. Fragment navigation is ignored (no DOM anchor support). */
static int resolve_url_from(const char *href, const char *base_url,
                            char out[NAV_URL_CAP])
{
    if (!href || !*href) return 0;
    unsigned scheme = scheme_len(href);
    if (scheme) {
        if (!copy_string(out,NAV_URL_CAP,href)) return 0;
    } else {
        unsigned base_scheme = scheme_len(base_url);
        if (!base_scheme) return 0;
        if (href[0]=='#') return copy_string(out,NAV_URL_CAP,base_url);
        if (href[0]=='/' && href[1]=='/') {
            size_t n = str_len(href);
            if (base_scheme - 2 + n >= NAV_URL_CAP) return 0;
            size_t j = 0;
            for (unsigned i=0;i<base_scheme-2;i++) out[j++]=base_url[i];
            for (size_t i=0;i<=n;i++) out[j+i]=href[i];
        } else {
            /* Reject unknown schemes rather than treating them as paths. */
            for (size_t i=0;href[i] && href[i]!='/' && href[i]!='?' && href[i]!='#';i++)
                if (href[i]==':') return 0;
            size_t base_len=str_len(base_url), authority=base_len;
            for (size_t i=base_scheme;i<base_len;i++) {
                if(base_url[i]=='/' || base_url[i]=='?' || base_url[i]=='#') {authority=i;break;}
            }
            size_t cut=authority;
            if (href[0]=='/') {
                /* Keep only scheme + authority. */
            } else if (href[0]=='?') {
                cut=base_len;
                for (size_t i=authority;i<base_len;i++)
                    if(base_url[i]=='?' || base_url[i]=='#') {cut=i;break;}
                if (cut==authority) { /* No path yet. */
                    if(cut+1>=NAV_URL_CAP) return 0;
                    for(size_t i=0;i<cut;i++) out[i]=base_url[i];
                    out[cut++]='/';
                }
            } else {
                cut=authority;
                for (size_t i=authority;i<base_len && base_url[i]!='?' && base_url[i]!='#';i++)
                    if (base_url[i]=='/') cut=i+1;
            }
            size_t n=str_len(href), j=cut;
            if(j+n+2>=NAV_URL_CAP) return 0;
            for(size_t i=0;i<j;i++) out[i]=base_url[i];
            if (href[0]!='/' && href[0]!='?' && (j==0 || out[j-1]!='/')) out[j++]='/';
            for(size_t i=0;i<=n;i++) out[j+i]=href[i];
        }
    }
    /* Fragments are not transmitted in HTTP requests. */
    for (size_t i=0;out[i];i++) if(out[i]=='#') {out[i]=0;break;}
    return 1;
}

int browser_queue_url(const char *href)
{
    char resolved[NAV_URL_CAP];
    if (!resolve_url_from(href,current_url,resolved)) {
        console_print("Browser: unsupported URL (HTTP/HTTPS only)\n");
        return 0;
    }
    if (!fetching && !pending && document_valid && same(current_url,resolved)) return 1;
    if (pending && same(pending_url,resolved)) return 1;
    if (fetching && !pending && same(request_url,resolved)) return 1;
    if (!copy_string(pending_url,sizeof pending_url,resolved)) return 0;
    navigation_mode=0;
    redirect_count=0;
    pending=1;
    return 1;
}

const char *browser_current_url(void) { return current_url; }
int browser_secure_page(void) { return document_valid && prefix(current_url,"https://"); }
int browser_can_back(void) { return (!document_valid && history_index>=0) || history_index>0; }
int browser_can_forward(void) { return history_index>=0 && history_index+1<history_count; }
int browser_go_back(void) {
    if(!browser_can_back()) return 0;
    int next = document_valid ? history_index-1 : history_index;
    if(!copy_string(pending_url,sizeof pending_url,history[next])) return 0;
    navigation_mode=document_valid?-1:2;
    redirect_count=0; pending=1; return 1;
}
int browser_go_forward(void) {
    if(!browser_can_forward()) return 0;
    if(!copy_string(pending_url,sizeof pending_url,history[history_index+1])) return 0;
    navigation_mode=1; redirect_count=0; pending=1; return 1;
}

static void cancel_request(void)
{
    if (fetching) {
        if (request_secure) tls_https_cancel();
        else http_async_cancel();
    }
    fetching=0;
    if (active_html) { kfree(active_html); active_html=0; }
}

void browser_reset_to_local_page(void)
{
    cancel_request();
    pending=0;
    navigation_mode=redirect_count=0;
    current_url[0]=request_url[0]=pending_url[0]=0;
    history_count=0; history_index=-1;
    document_valid=0;
}

static int parse_url(const char *url, int *secure, uint16_t *port,
                     char host[128], char path[256])
{
    unsigned scheme=scheme_len(url);
    if (!scheme) return 0;
    *secure=(scheme==8);
    *port=*secure?443:80;
    const char *p=url+scheme;
    size_t n=0;
    while(*p && *p!='/' && *p!='?' && *p!='#' && *p!=':') {
        char ch=*p++;
        if (!((ch>='a'&&ch<='z') || (ch>='A'&&ch<='Z') ||
              (ch>='0'&&ch<='9') || ch=='.' || ch=='-') || n+1>=128) return 0;
        host[n++]=ch;
    }
    if (!n) return 0;
    host[n]=0;
    if (*p==':') {
        p++; unsigned value=0,digits=0;
        while(*p>='0' && *p<='9') {
            value=value*10u+(unsigned)(*p++-'0');
            if(value>65535u) return 0;
            digits++;
        }
        if(!digits || !value) return 0;
        *port=(uint16_t)value;
    }
    if (*p && *p!='/' && *p!='?' && *p!='#') return 0;
    size_t j=0;
    if (*p!='/') path[j++]='/';
    while(*p && *p!='#') {
        unsigned char c=(unsigned char)*p++;
        if(c<=32 || c>=127 || j+1>=256) return 0;
        path[j++]=(char)c;
    }
    path[j]=0;
    return 1;
}

static int redirect_status(int code)
{
    return code==301 || code==302 || code==303 || code==307 || code==308;
}

static void update_history(void)
{
    if(navigation_mode==-1 && history_index>0) history_index--;
    else if(navigation_mode==1 && history_index+1<history_count) history_index++;
    else if(navigation_mode!=2 && (history_index<0 || !same(history[history_index],current_url))) {
        history_count=history_index+1;
        if(history_count>=NAV_HISTORY_MAX) {
            for(int i=1;i<history_count;i++)
                copy_string(history[i-1],NAV_URL_CAP,history[i]);
            history_count--; history_index--;
        }
        copy_string(history[history_count],NAV_URL_CAP,current_url);
        history_index=history_count++;
    }
    navigation_mode=0;
    redirect_count=0;
}

int browser_navigation_poll(void)
{
    /* The diagnostic and browser HTTP share a single NIC RX queue. */
    if (pending && tls_probe_active()) return 0;
    if (pending) {
        pending=0;
        cancel_request();
        char host[128], path[256]; uint16_t port; int secure;
        if(!copy_string(request_url,sizeof request_url,pending_url)) return 1;
        if(!parse_url(request_url,&secure,&port,host,path)) {
            console_print("Browser: invalid HTTP/HTTPS URL\n"); return 1;
        }
        if (secure) {
            uint32_t days,seconds;
            if (!tls_clock_now(&days,&seconds)) {
                console_print("Browser: HTTPS requires approved UTC; use tlsclock copenhagen or tlsclock set\n");
                return 1; /* Preserve the old page when the clock is missing. */
            }
        }
        if(!network_ensure_ready()) {
            console_print("Browser: network initialization failed\n"); return 1;
        }
        /* Only one Lexbor document and stylesheet at a time. */
        browser_discard_document();
        document_valid=0;
        active_html=kmalloc(NAV_PAGE_CAP);
        if(!active_html) {console_print("Browser: out of memory\n");return 1;}
        request_secure=secure;
        int started=secure ? tls_https_start(host,port,path,active_html,NAV_PAGE_CAP) :
                             http_async_start(host,port,path,active_html,NAV_PAGE_CAP);
        if(!started) {
            console_print("Browser: ");
            console_print(secure ? tls_https_error() : "unable to start HTTP request");
            console_print("\n");
            kfree(active_html); active_html=0;
            return 1;
        }
        fetching=1;
        console_print("Browser: loading ");console_print(request_url);console_print("\n");
        return 1;
    }
    if(!fetching) return 0;
    int result=request_secure?tls_https_poll():http_async_poll();
    if(result==0) return 0;
    fetching=0;
    if(result<0) {
        console_print("Browser: ");
        console_print(request_secure?tls_https_error():http_async_error());
        console_print("\n");
        if(request_secure) tls_https_cancel(); else http_async_cancel();
        kfree(active_html);active_html=0;return 1;
    }
    int status_code=request_secure?tls_https_status():http_async_status();
    if(redirect_status(status_code)) {
        char next[NAV_URL_CAP];
        const char *location=request_secure?tls_https_location():http_async_location();
        if(++redirect_count>5 || !resolve_url_from(location,request_url,next) ||
           (request_secure && !prefix(next,"https://"))) {
            console_print("Browser: invalid, insecure or excessive redirect\n");
            if(request_secure) tls_https_cancel(); else http_async_cancel();
            kfree(active_html);active_html=0;return 1;
        }
        if(request_secure) tls_https_cancel(); else http_async_cancel();
        kfree(active_html);active_html=0;
        copy_string(pending_url,sizeof pending_url,next);
        pending=1;
        return 1;
    }
    if(status_code!=200) {
        console_print("Browser: unsupported HTTP response status\n");
        if(request_secure) tls_https_cancel(); else http_async_cancel();
        kfree(active_html);active_html=0;return 1;
    }
    size_t len=request_secure?tls_https_length():http_async_length();
    lxb_html_document_t *doc=lxb_html_document_create();
    if(!doc) {
        if(request_secure) tls_https_cancel(); else http_async_cancel();
        kfree(active_html);active_html=0;return 1;
    }
    lxb_status_t parse_status=lxb_html_document_parse(doc,(const lxb_char_t*)active_html,len);
    if(request_secure) tls_https_cancel(); else http_async_cancel();
    kfree(active_html);active_html=0;
    if(parse_status!=LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        console_print("Browser: HTML parse failed\n");return 1;
    }
    copy_string(current_url,sizeof current_url,request_url);
    document_valid=1;
    update_history();
    browser_open(doc);
    console_print("Browser: page loaded");
    if(request_secure) console_print(" (verified HTTPS)");
    console_print("\n");
    return 1;
}
