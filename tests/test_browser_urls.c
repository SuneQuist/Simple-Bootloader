/* Unit-test the actual browser URL resolver without pulling in Lexbor runtime.
 * Unreferenced navigation functions are garbage-collected by the host linker. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../web/navigation.c"
void console_print(const char *s) { (void)s; }

static void check(const char *href, const char *base, const char *want)
{
    char out[NAV_URL_CAP];
    assert(resolve_url_from(href,base,out));
    assert(strcmp(out,want)==0);
}
int main(void)
{
    char host[128], path[256]; uint16_t port; int secure;
    check("https://example.com/", "", "https://example.com/");
    check("http://example.com/", "", "http://example.com/");
    check("/about", "https://example.com/dir/page", "https://example.com/about");
    check("next", "https://example.com/dir/page", "https://example.com/dir/next");
    check("next", "https://example.com", "https://example.com/next");
    check("//example.org/x", "https://example.com/a", "https://example.org/x");
    check("?x=1", "https://example.com/page", "https://example.com/page?x=1");
    check("#anchor", "https://example.com/page", "https://example.com/page");
    check("https://example.com/a#frag", "", "https://example.com/a");
    assert(!resolve_url_from("ftp://example.com", "https://example.com/",(char[NAV_URL_CAP]){0}));
    assert(parse_url("https://example.com/test?a=1",&secure,&port,host,path));
    assert(secure && port==443 && !strcmp(host,"example.com") && !strcmp(path,"/test?a=1"));
    assert(parse_url("http://example.com",&secure,&port,host,path));
    assert(!secure && port==80 && !strcmp(path,"/"));
    assert(parse_url("https://example.com:8443?x=1",&secure,&port,host,path));
    assert(secure && port==8443 && !strcmp(path,"/?x=1"));
    assert(!parse_url("https://bad@host/",&secure,&port,host,path));
    assert(!parse_url("https://example.com:99999/",&secure,&port,host,path));
    puts("PASS: HTTP/HTTPS URL parsing, redirects, relative paths, fragments and ports");
    return 0;
}
