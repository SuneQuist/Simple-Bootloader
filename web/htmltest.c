#include <stddef.h>

#include <lexbor/html/html.h>

#include "../console/console.h"

#include "htmltest.h"
#include "renderer.h"
#include "navigation.h"
#include "css.h"

void htmltest_run(void)
{
    /*
     * If the test browser already has a parsed
     * document, simply reopen its existing window.
     *
     * No new Lexbor allocations are necessary.
     */
    if (browser_reopen()) {
        console_print("HTML: reopening existing browser\n");
        return;
    }

    /*
     * First launch: create and parse the document.
     *
     * Keep the rest of your existing htmltest code
     * unchanged below this point.
     */
    
    console_print("HTML: creating document...\n");

    lxb_html_document_t *doc = lxb_html_document_create();

    if (doc == NULL) {
        console_print("HTML: document creation FAILED\n");
        return;
    }

    const char html[] =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<style>"
        "body { background-color: #e8f0ff; }"
        "h1 { color: #2463eb; }"
        ".card { background-color: #ffffff; color: #167347; padding: 20px; margin: 12px 16px; }"
        "#special { color: #c026d3; }"
        "</style>"
        "<title>Tiny64 Windows!</title>"
        "</head>"
        "<body>"
        "<h1>Hello Tiny64!</h1>"
        "<p>Welcome to my operating system.</p>"
        "<div class='card'>"
        "<p>This text should be green.</p>"
        "</div>"
        "<p><a href='https://example.com/'>HTTPS example (verified TLS)</a></p>"
        "<p><a href='http://example.com/'>HTTP example.com (DNS test)</a></p>"
        "<p>Scroll down to test the long page:</p>"
        "<p>Line 01 - Tiny64 browser scrolling.</p>"
        "<p>Line 02 - Tiny64 browser scrolling.</p>"
        "<p>Line 03 - Tiny64 browser scrolling.</p>"
        "<p>Line 04 - Tiny64 browser scrolling.</p>"
        "<p>Line 05 - Tiny64 browser scrolling.</p>"
        "<p>Line 06 - Tiny64 browser scrolling.</p>"
        "<p>Line 07 - Tiny64 browser scrolling.</p>"
        "<p>Line 08 - Tiny64 browser scrolling.</p>"
        "<p>Line 09 - Tiny64 browser scrolling.</p>"
        "<p>Line 10 - Tiny64 browser scrolling.</p>"
        "<p>Line 11 - Tiny64 browser scrolling.</p>"
        "<p>Line 12 - Tiny64 browser scrolling.</p>"
        "<p>Line 13 - Tiny64 browser scrolling.</p>"
        "<p>Line 14 - Tiny64 browser scrolling.</p>"
        "<p>Line 15 - Tiny64 browser scrolling.</p>"
        "<p>Line 16 - Tiny64 browser scrolling.</p>"
        "<p>Line 17 - Tiny64 browser scrolling.</p>"
        "<p>Line 18 - Tiny64 browser scrolling.</p>"
        "<p>Line 19 - Tiny64 browser scrolling.</p>"
        "<p>Line 20 - Tiny64 browser scrolling.</p>"
        "<p id='special'>This text should be purple!</p>"
        "</body>"
        "</html>";

    console_print("HTML: parsing...\n");

    lxb_status_t status = lxb_html_document_parse(
        doc,
        (const lxb_char_t *)html,
        sizeof(html) - 1
    );

    if (status != LXB_STATUS_OK) {
        console_print("HTML: parsing FAILED\n");
        return;
    }

    console_print("HTML: parsing OK!\n");

    console_print("HTML: rendering page...\n");

    /*
    * Draw the browser last.
    * Don't print anything afterward.
    */
    browser_reset_to_local_page();
    browser_open(doc);

    /*
    * Verify that the document's CSS was parsed.
    */
    if (css_get_stylesheet() != NULL)
        console_print("Document CSS loaded!\n");
    else
        console_print("Document CSS NOT loaded!\n");

    /*
     * Cleanup remains disabled while we debug
     * Lexbor document destruction.
     */
}
