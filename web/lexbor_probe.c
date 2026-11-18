#include <lexbor/html/html.h>

int lexbor_probe(void)
{
    lxb_html_document_t *doc =
        lxb_html_document_create();

    if (doc == 0)
        return 0;

    lxb_html_document_destroy(doc);

    return 1;
}
