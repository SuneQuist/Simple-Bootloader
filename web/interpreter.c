#include <stddef.h>

#include <lexbor/html/html.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/dom/interfaces/character_data.h>

#include "../console/console.h"
#include "interpreter.h"

#define HTML_TEXT_BUFFER 256
#define HTML_MAX_DEPTH   32

static void html_print_text(lxb_dom_node_t *node)
{
    lxb_dom_character_data_t *text =
        lxb_dom_interface_character_data(node);

    if (text == NULL || text->data.data == NULL) {
        return;
    }

    size_t length = text->data.length;
    size_t offset = 0;

    /*
     * Lexbor text is length-delimited, so copy it
     * into a null-terminated buffer before printing.
     */
    while (offset < length) {
        char buffer[HTML_TEXT_BUFFER];

        size_t count = length - offset;

        if (count >= sizeof(buffer)) {
            count = sizeof(buffer) - 1;
        }

        for (size_t i = 0; i < count; i++) {
            buffer[i] = (char)text->data.data[offset + i];
        }

        buffer[count] = '\0';

        console_print(buffer);

        offset += count;
    }
}

static int html_tag_equals(lxb_dom_node_t *node, const char *name)
{
    if (node == NULL || node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return 0;
    }

    lxb_dom_element_t *element = lxb_dom_interface_element(node);

    size_t length = 0;
    const lxb_char_t *tag = lxb_dom_element_local_name(element, &length);

    if (tag == NULL) {
        return 0;
    }

    size_t i = 0;

    while (name[i] != '\0' && i < length) {
        char a = (char)tag[i];
        char b = name[i];

        /* ASCII case-insensitive comparison. */
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }

        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }

        if (a != b) {
            return 0;
        }

        i++;
    }

    return i == length && name[i] == '\0';
}

static int html_is_heading(lxb_dom_node_t *node)
{
    return html_tag_equals(node, "h1") ||
           html_tag_equals(node, "h2") ||
           html_tag_equals(node, "h3") ||
           html_tag_equals(node, "h4") ||
           html_tag_equals(node, "h5") ||
           html_tag_equals(node, "h6");
}

static void html_interpret_node(lxb_dom_node_t *node, unsigned depth)
{
    if (node == NULL || depth >= HTML_MAX_DEPTH) {
        return;
    }

    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        html_print_text(node);
        return;
    }

    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return;
    }

    int heading = html_is_heading(node);
    int paragraph = html_tag_equals(node, "p");
    int division = html_tag_equals(node, "div");
    int line_break = html_tag_equals(node, "br");

    if (line_break) {
        console_print("\n");
        return;
    }

    if (heading) {
        console_print("\n[HEADING] ");
    }
    else if (paragraph) {
        console_print("\n");
    }
    else if (division) {
        console_print("\n");
    }

    /*
     * Recursively interpret children.
     *
     * A span doesn't introduce a line break.
     * Unknown elements are treated as containers.
     */
    for (lxb_dom_node_t *child = node->first_child;
         child != NULL;
         child = child->next)
    {
        html_interpret_node(child, depth + 1);
    }

    if (heading) {
        console_print("\n");
    }
    else if (paragraph || division) {
        console_print("\n");
    }
}

void html_interpret(lxb_html_document_t *document)
{
    if (document == NULL || document->body == NULL) {
        console_print("HTML: no body to interpret\n");
        return;
    }

    lxb_dom_node_t *body =
        lxb_dom_interface_node(document->body);

    console_print("\n--- HTML PAGE ---\n");

    for (lxb_dom_node_t *child = body->first_child;
         child != NULL;
         child = child->next)
    {
        html_interpret_node(child, 0);
    }

    console_print("\n--- END PAGE ---\n");
}