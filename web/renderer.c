#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include "../console/console.h"

#include <lexbor/html/html.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/character_data.h>

#include "../graphics/framebuffer.h"
#include "../graphics/font.h"
#include "../graphics/window.h"
#include "../graphics/desktop.h"

#include "renderer.h"
#include "css.h"
#include "navigation.h"

/*
 * Adjust these includes if your framebuffer and font
 * headers live in another directory.
 */

#define HTML_MAX_DEPTH 32

#define WINDOW_X 100
#define WINDOW_Y 80
#define WINDOW_W 700
#define WINDOW_H 500

#define PAGE_PADDING 20

#define COLOR_WINDOW  0x00FFFFFF
#define COLOR_TITLE   0x0023344B
#define COLOR_TEXT    0x00182738
#define COLOR_BORDER  0x00AAB5C4

/* HTML */
typedef struct {
    int x;
    int y;
    int left;
    int right;
    int bottom;
    int scale;
    int line_height;
    uint32_t color;

    /*
     * When set, calculate text positions without
     * writing pixels to the framebuffer.
     */
    int measure_only;
    const lxb_char_t *link_href;
    size_t link_href_length;
} html_draw_context_t;

#define BROWSER_LINK_HITS 2048
#define BROWSER_SCROLL_STEP 45
#define BROWSER_VIEW_TOP 72
#define BROWSER_VIEW_BOTTOM 2

typedef struct {
    int x, y, w, h;
    const lxb_char_t *href;
    size_t href_length;
} browser_link_hit_t;

static browser_link_hit_t browser_links[BROWSER_LINK_HITS];
static unsigned browser_link_count;
static int browser_scroll_y;
static int browser_content_height;
static int browser_view_height;
static int browser_window_id = -1;
static lxb_html_document_t *browser_document = NULL;
#define ADDRESS_CAP 256
static char browser_address[ADDRESS_CAP];
static unsigned browser_address_len;
static int browser_address_editing;

static void browser_address_set(const char *value) {
    unsigned n=0;
    while(value && value[n] && n+1<ADDRESS_CAP) { browser_address[n]=value[n]; n++; }
    browser_address[n]=0;browser_address_len=n;
}

static void browser_address_clear(void)
{
    browser_address[0] = '\0';
    browser_address_len = 0;
}

static int html_tag_equals(lxb_dom_node_t *node, const char *name)
{
    if (node == NULL || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return 0;

    lxb_dom_element_t *element = lxb_dom_interface_element(node);

    size_t length = 0;
    const lxb_char_t *tag =
        lxb_dom_element_local_name(element, &length);

    if (tag == NULL)
        return 0;

    size_t i = 0;

    while (name[i] && i < length) {
        char a = (char)tag[i];
        char b = name[i];

        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');

        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');

        if (a != b)
            return 0;

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

static void html_newline(html_draw_context_t *ctx)
{
    ctx->x = ctx->left;
    ctx->y += ctx->line_height;
}

static void html_draw_character(html_draw_context_t *ctx, char c)
{
    int advance = 7 * ctx->scale;

    if (c == '\n') {
        html_newline(ctx);
        return;
    }

    if (ctx->x + advance > ctx->right)
        html_newline(ctx);

    /* Do not truncate layout: the framebuffer clip handles visibility. */

    /*
    * The measurement pass uses exactly the same
    * character spacing and wrapping as painting.
    */
    if (!ctx->measure_only) {
        if (ctx->link_href && browser_link_count < BROWSER_LINK_HITS) {
            browser_link_hit_t *hit = &browser_links[browser_link_count++];
            hit->x = ctx->x; hit->y = ctx->y;
            hit->w = advance; hit->h = 15 * ctx->scale;
            hit->href = ctx->link_href;
            hit->href_length = ctx->link_href_length;
        }
        fb_char(ctx->x, ctx->y, c, ctx->color, ctx->scale);
    }

    ctx->x += advance;
}

static void html_draw_text(
    html_draw_context_t *ctx,
    lxb_dom_node_t *node
)
{
    lxb_dom_character_data_t *text =
        lxb_dom_interface_character_data(node);

    if (text == NULL || text->data.data == NULL)
        return;

    for (size_t i = 0; i < text->data.length; i++) {
        char c = (char)text->data.data[i];
        if (c == '\r' || c == '\t' || c == '\n') c=' ';
        /* Wrap before words, not halfway through them. Long words still
         * use character-level wrapping in html_draw_character(). */
        if (c!=' ' && (i==0 || text->data.data[i-1]==' ' ||
                        text->data.data[i-1]=='\n' || text->data.data[i-1]=='\t')) {
            size_t j=i;
            while(j<text->data.length && text->data.data[j]!=' ' &&
                  text->data.data[j]!='\n' && text->data.data[j]!='\t' &&
                  text->data.data[j]!='\r') j++;
            int word_pixels=(int)(j-i)*7*ctx->scale;
            if(ctx->x>ctx->left && ctx->x+word_pixels>ctx->right)
                html_newline(ctx);
        }
        if(c==' ' && ctx->x==ctx->left) continue;
        html_draw_character(ctx, c);
    }
}

/*
 * Forward declaration so block rendering can
 * recursively use the normal HTML renderer.
 */
static void html_render_node(
    html_draw_context_t *ctx,
    lxb_dom_node_t *node,
    unsigned depth
);

/*
 * Render a block element with its own background.
 *
 * First measure its contents, then draw the
 * background, and finally paint its contents.
 *
 * This initial version uses fixed padding.
 * CSS-controlled padding will be added next.
 */
static void html_render_box(
    html_draw_context_t *ctx,
    lxb_dom_node_t *node,
    unsigned depth,
    const css_computed_style_t *style
)
{

    /*
    * Start the block on a new line.
    */
    if (ctx->x != ctx->left)
        html_newline(ctx);

    /*
    * CSS margins are outside the border box.
    * Padding is inside the border box.
    */
    int outer_left = ctx->left + style->margin_left;
    int outer_right = ctx->right - style->margin_right;
    int outer_top = ctx->y + style->margin_top;

    int inner_left = outer_left + style->padding_left;
    int inner_right = outer_right - style->padding_right;

    if (outer_right <= outer_left ||
        inner_right <= inner_left)
    {
        return;
    }

    /*
     * Measure children using a copy of the
     * current rendering context.
     */
    html_draw_context_t measured = *ctx;

    measured.measure_only = 1;
    measured.left = inner_left;
    measured.right = inner_right;
    measured.x = inner_left;
    measured.y = outer_top + style->padding_top;

    for (lxb_dom_node_t *child = node->first_child;
         child != NULL;
         child = child->next)
    {
        html_render_node(&measured, child, depth + 1);
    }

    /*
     * Include the final line of text in the
     * measured height.
     */
    int content_bottom = measured.y + measured.line_height;
    int box_bottom = content_bottom + style->padding_bottom;


    /*
     * Draw the background before painting text.
     *
     * Never draw framebuffer pixels during the
     * measurement pass.
     */
    if (style->has_background &&
        !ctx->measure_only &&
        box_bottom > outer_top)
    {
        fb_rect(
            outer_left,
            outer_top,
            outer_right - outer_left,
            box_bottom - outer_top,
            style->background_color
        );
    }

    /*
     * Paint the children at their measured positions.
     */
    html_draw_context_t painted = *ctx;

    painted.left = inner_left;
    painted.right = inner_right;
    painted.x = inner_left;
    painted.y = outer_top + style->padding_top;

    for (lxb_dom_node_t *child = node->first_child;
         child != NULL;
         child = child->next)
    {
        html_render_node(&painted, child, depth + 1);
    }

    /*
     * Continue the parent layout below the box.
     */
    ctx->x = ctx->left;
    ctx->y = box_bottom + style->margin_bottom;
}

static void html_render_node(
    html_draw_context_t *ctx,
    lxb_dom_node_t *node,
    unsigned depth
)
{
    if (node == NULL || depth >= HTML_MAX_DEPTH)
        return;

    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        html_draw_text(ctx, node);
        return;
    }

    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return;

    int heading = html_is_heading(node);
    int paragraph = html_tag_equals(node, "p");
    int division = html_tag_equals(node, "div");
    int br = html_tag_equals(node, "br");
    int anchor = html_tag_equals(node, "a");

    if (br) {
        html_newline(ctx);
        return;
    }

    /*
    * Preserve the parent element's text styling.
    *
    * CSS color is inherited by child elements, but
    * a child's color must not leak into its siblings.
    */
    int old_scale = ctx->scale;
    int old_line_height = ctx->line_height;
    uint32_t old_color = ctx->color;
    const lxb_char_t *old_href = ctx->link_href;
    size_t old_href_length = ctx->link_href_length;
    if (anchor) {
        size_t href_length = 0;
        const lxb_char_t *href = lxb_dom_element_get_attribute(
            lxb_dom_interface_element(node),
            (const lxb_char_t *)"href", 4, &href_length);
        ctx->link_href = href;
        ctx->link_href_length = href_length;
    }

    /*
    * Compute the element's CSS style.
    *
    * Text color inherits from the current
    * rendering context.
    */
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        css_computed_style_t style = {
            .color = ctx->color,
            .background_color = 0,
            .has_background = 0
        };

        css_compute_style(node, &style);

        ctx->color = style.color;

        /*
        * A div with a CSS background is rendered
        * as a measured block box.
        *
        * Divs without a background keep their
        * existing rendering behavior for now.
        */
        if (division &&
            (style.has_background ||
            style.padding_top ||
            style.padding_right ||
            style.padding_bottom ||
            style.padding_left ||
            style.margin_top ||
            style.margin_right ||
            style.margin_bottom ||
            style.margin_left))
        {
            html_render_box(ctx, node, depth, &style);

            ctx->scale = old_scale;
            ctx->line_height = old_line_height;
            ctx->color = old_color;
            ctx->link_href = old_href;
            ctx->link_href_length = old_href_length;
            return;
        }
    }

    if (heading || paragraph || division) {
        if (ctx->x != ctx->left)
            html_newline(ctx);
    }

    if (heading) {
        ctx->scale = html_tag_equals(node, "h1") ? 2 : 1;
        ctx->line_height = 15 * ctx->scale;
    }

    for (lxb_dom_node_t *child = node->first_child;
         child != NULL;
         child = child->next)
    {
        html_render_node(ctx, child, depth + 1);
    }

    if (heading || paragraph || division) {
        html_newline(ctx);
        ctx->y += heading ? 8 : 5;
    }

    ctx->scale = old_scale;
    ctx->line_height = old_line_height;
    ctx->color = old_color;
    ctx->link_href = old_href;
    ctx->link_href_length = old_href_length;
}

/*
 * Extract the HTML document's title.
 *
 * The title comes from:
 *
 *     <head>
 *         <title>Page title</title>
 *     </head>
 *
 * The returned text is copied into the caller's buffer,
 * so it remains valid independently of Lexbor's internal
 * DOM string storage.
 *
 * If no title is available, use a default browser title.
 */
static void browser_get_document_title(
    lxb_html_document_t *document,
    char *buffer,
    size_t capacity
)
{
    const char fallback[] = "Tiny64 Browser";

    if (buffer == NULL || capacity == 0)
        return;

    buffer[0] = '\0';

    if (document != NULL) {
        /*
         * Lexbor stores the parsed document title
         * as a DOM element in the document head.
         */
        lxb_dom_node_t *head =
            (lxb_dom_node_t *)document->head;

        if (head != NULL) {
            for (lxb_dom_node_t *node = head->first_child;
                 node != NULL;
                 node = node->next) {

                if (node->type != LXB_DOM_NODE_TYPE_ELEMENT)
                    continue;

                if (node->local_name != LXB_TAG_TITLE)
                    continue;

                /*
                 * The title element normally contains
                 * a text node with the page title.
                 */
                size_t length = 0;

                for (lxb_dom_node_t *child = node->first_child;
                     child != NULL;
                     child = child->next) {

                    if (child->type != LXB_DOM_NODE_TYPE_TEXT)
                        continue;

                    lxb_dom_character_data_t *text =
                        (lxb_dom_character_data_t *)child;

                    size_t text_length = text->data.length;

                    if (length + text_length >= capacity)
                        text_length = capacity - length - 1;

                    for (size_t i = 0; i < text_length; i++) {
                        buffer[length++] = text->data.data[i];
                    }

                    if (length >= capacity - 1)
                        break;
                }

                buffer[length] = '\0';

                if (length != 0)
                    return;
            }
        }
    }

    /*
     * Use the fallback if the document doesn't
     * contain a nonempty title.
     */
    size_t i = 0;

    while (fallback[i] != '\0' && i + 1 < capacity) {
        buffer[i] = fallback[i];
        i++;
    }

    buffer[i] = '\0';
}

void html_render_at(
    lxb_html_document_t *document,
    int window_x,
    int window_y
)
{
    /* Keep the toolbar visible while a page is loading or after failure. */
    /* Browser window shadow */
    fb_rect(
        window_x + 5,
        window_y + 5,
        WINDOW_W,
        WINDOW_H,
        0x000D1725
    );

    /*
    * Compute the body's CSS style once.
    *
    * Its text color is inherited by the elements
    * rendered inside the document body.
    */
    css_computed_style_t body_style = {
        .color = COLOR_TEXT,
        .background_color = 0,
        .has_background = 0
    };

    if (document && document->body)
        css_compute_style((lxb_dom_node_t *)document->body, &body_style);

    /*
    * Use the body's CSS background color if one
    * was specified. Otherwise, use white.
    */
    uint32_t page_background =
        body_style.has_background
            ? body_style.background_color
            : COLOR_WINDOW;

    /*
    * Browser window body.
    *
    * Use the background color computed from the
    * HTML body's CSS declarations.
    */
    fb_rect(
        window_x,
        window_y,
        WINDOW_W,
        WINDOW_H,
        page_background
    );

    /* Title bar */
    fb_rect(
        window_x,
        window_y,
        WINDOW_W,
        36,
        COLOR_TITLE
    );

    /* Window border */
    fb_rect(
        window_x,
        window_y + WINDOW_H - 2,
        WINDOW_W,
        2,
        COLOR_BORDER
    );
    
    /*
    * Read the title from the HTML document.
    *
    * Example:
    *     <title>My Website</title>
    *
    * becomes the window's displayed title.
    */
    char window_title[128];

    browser_get_document_title(
        document,
        window_title,
        sizeof(window_title)
    );

    /*
    * Draw the document title in the browser's
    * title bar.
    */
    fb_text(
        window_x + 12,
        window_y + 11,
        window_title,
        0x00FFFFFF,
        1
    );

    /* Close button (visual only for now) */
    fb_rect(
        window_x + WINDOW_W - 34,
        window_y + 8,
        24,
        20,
        0x00D94A4A
    );

    /* Navigation toolbar occupies the region below the window title. */
    fb_rect(window_x,window_y+36,WINDOW_W,36,0x00DDE5EF);
    fb_rect(window_x+8,window_y+42,28,24,0x00FFFFFF);
    fb_rect(window_x+42,window_y+42,28,24,0x00FFFFFF);
    fb_text(window_x+17,window_y+47,"<",browser_can_back()?COLOR_TEXT:0x0099A1AB,1);
    fb_text(window_x+51,window_y+47,">",browser_can_forward()?COLOR_TEXT:0x0099A1AB,1);
    fb_rect(window_x+78,window_y+42,WINDOW_W-88,24,0x00FFFFFF);
    int verified_https = document && browser_secure_page();
    if (verified_https) {
        /* Small green padlock: shown only for a verified, loaded page. */
        fb_rect(window_x+85,window_y+53,8,8,0x00157839);
        fb_rect(window_x+87,window_y+48,4,5,0x00157839);
        fb_rect(window_x+88,window_y+49,2,4,0x00FFFFFF);
        fb_text(window_x+97,window_y+48,"TLS",0x00157839,1);
    }
    if(!browser_address_editing)
        browser_address_set(browser_current_url());
    /* Limit displayed text to the visible width without changing the URL. */
    char address_view[84];
    unsigned max_chars=(unsigned)((WINDOW_W-(verified_https?146:106))/7);
    if(max_chars>sizeof address_view-1) max_chars=sizeof address_view-1;
    unsigned start=browser_address_len>max_chars?browser_address_len-max_chars:0;
    unsigned n=0;
    while(start+n<browser_address_len && n<max_chars) {
        address_view[n]=browser_address[start+n];n++;
    }
    address_view[n]=0;
    int text_x=window_x+(verified_https?125:85);
    fb_text(text_x,window_y+48,address_view,COLOR_TEXT,1);
    if(browser_address_editing && n<max_chars)
        fb_text(text_x+(int)n*7,window_y+48,"_",COLOR_TEXT,1);

    if (!document || !document->body) {
        browser_link_count=0;
        fb_text(window_x+PAGE_PADDING,window_y+BROWSER_VIEW_TOP+PAGE_PADDING,
                "Loading or no page available",COLOR_TEXT,1);
        return;
    }

    html_draw_context_t ctx;
    browser_link_count = 0;

    ctx.left = window_x + PAGE_PADDING;
    ctx.right = window_x + WINDOW_W - PAGE_PADDING;
    ctx.bottom = INT_MAX / 4;
    ctx.measure_only = 0;

    ctx.x = ctx.left;
    ctx.y = window_y + BROWSER_VIEW_TOP + PAGE_PADDING - browser_scroll_y;

    ctx.scale = 1;
    ctx.line_height = 15;
    ctx.color = body_style.color;
    ctx.link_href = NULL;
    ctx.link_href_length = 0;

    /* Measure total page height before drawing, for scroll clamping. */
    html_draw_context_t measured = ctx;
    measured.measure_only = 1;
    measured.y = window_y + BROWSER_VIEW_TOP + PAGE_PADDING;
    measured.x = measured.left;
    lxb_dom_node_t *measure_body = lxb_dom_interface_node(document->body);
    for (lxb_dom_node_t *n = measure_body->first_child; n; n = n->next)
        html_render_node(&measured, n, 0);
    browser_content_height = measured.y + measured.line_height -
        (window_y + BROWSER_VIEW_TOP);
    browser_view_height = WINDOW_H - BROWSER_VIEW_TOP - BROWSER_VIEW_BOTTOM;
    int max_scroll = browser_content_height - browser_view_height;
    if (max_scroll < 0) max_scroll = 0;
    if (browser_scroll_y > max_scroll) browser_scroll_y = max_scroll;
    ctx.y = window_y + BROWSER_VIEW_TOP + PAGE_PADDING - browser_scroll_y;

    fb_clip_set(window_x, window_y + BROWSER_VIEW_TOP,
                WINDOW_W, browser_view_height);

    lxb_dom_node_t *body =
        lxb_dom_interface_node(document->body);

    for (lxb_dom_node_t *node = body->first_child;
         node != NULL;
         node = node->next)
    {
        html_render_node(&ctx, node, 0);
    }
    fb_clip_reset();
}

void html_render(lxb_html_document_t *document)
{
    html_render_at(document, WINDOW_X, WINDOW_Y);
}

void browser_window_draw(void *context)
{
    (void)context;

    int x = 100;
    int y = 80;

    window_get_position(browser_window_id, &x, &y);

    html_render_at(browser_document, x, y);
}

/* The browser owns exactly one Lexbor document at a time.  This is
 * called before downloading the next page to keep peak heap usage low. */
void browser_discard_document(void)
{
    css_unload_document();
    browser_link_count = 0;
    browser_scroll_y = 0;
    if (browser_document) {
        lxb_html_document_destroy(browser_document);
        browser_document = NULL;
    }
    if (browser_window_id >= 0) window_request_redraw();
}

void browser_open(lxb_html_document_t *document)
{
    if (document == NULL)
        return;

    if (browser_document && browser_document != document)
        browser_discard_document();
    browser_document = document;
    browser_scroll_y = 0;

    /*
    * Parse embedded CSS once when opening a document.
    *
    * The browser redraw function will reuse this
    * cached stylesheet instead of parsing CSS again.
    */
    if (css_load_document(document)) {
        console_print("CSS: document loaded OK\n");
        css_debug_rules();
    } else {
        console_print("CSS: document loading FAILED\n");
    }

    /*
     * Register the browser window once.
     *
     * Later calls reuse the same window, preserving
     * its position and window-manager identity.
     */
    if (browser_window_id < 0) {
        browser_window_id = window_add(
            browser_window_draw,
            NULL,
            100, 80, 700, 500,
            20
        );

        if (browser_window_id < 0)
            return;
    }

    /*
     * The browser already reserves 36 pixels for
     * its title bar, so decorations are drawn inside
     * the existing window bounds.
     *
     * The close button hides the browser without
     * destroying its Lexbor document.
     */
    window_set_decorations(
        browser_window_id,
        1,  /* decorated */
        0,  /* title bar inside window */
        1   /* closable */
    );

    /*
     * Extract the title from the parsed HTML.
     *
     * The window manager copies this string, so
     * the local buffer can safely go out of scope.
     */
    char title[WINDOW_TITLE_MAX];

    browser_get_document_title(
        document,
        title,
        sizeof(title)
    );

    window_set_title(browser_window_id, title);

    /*
     * Show the browser and bring it to the front.
     */
    window_set_visible(browser_window_id, 1);
    window_focus(browser_window_id);

    desktop_redraw();
}

/*
 * Reopen the browser using its existing document.
 *
 * Closing the browser only hides the window, so its
 * Lexbor document remains available in memory.
 *
 * This avoids allocating another document each time
 * the htmltest command is executed.
 */
int browser_reopen(void)
{
    if (browser_window_id < 0 || browser_document == NULL)
        return 0;

    browser_address_editing = 0;

    browser_address_set(browser_current_url());

    window_set_visible(browser_window_id, 1);
    window_focus(browser_window_id);

    desktop_redraw();

    return 1;
}

/* Wheel events are delivered only to the topmost browser viewport. */
int browser_mouse_wheel(int x, int y, int wheel)
{
    if (browser_window_id < 0 || browser_document == NULL || wheel == 0)
        return 0;
    if (window_at_point(x, y) != browser_window_id)
        return 0;
    int wx = 0, wy = 0;
    window_get_position(browser_window_id, &wx, &wy);
    if (x < wx || x >= wx + WINDOW_W ||
        y < wy + BROWSER_VIEW_TOP || y >= wy + WINDOW_H - BROWSER_VIEW_BOTTOM)
        return 0;
    int max_scroll = browser_content_height - browser_view_height;
    if (max_scroll < 0) max_scroll = 0;
    int next = browser_scroll_y - wheel * BROWSER_SCROLL_STEP;
    if (next < 0) next = 0;
    if (next > max_scroll) next = max_scroll;
    if (next != browser_scroll_y) {
        browser_scroll_y = next;
        window_request_redraw();
    }
    return 1;
}

/* Report the href of a clicked link. Navigation is a separate next step. */
int browser_mouse_click(int x, int y)
{
    if (browser_window_id < 0 ||
        window_at_point(x, y) != browser_window_id)
        return 0;
    int wx = 0, wy = 0;
    window_get_position(browser_window_id, &wx, &wy);
    if (x < wx || x >= wx + WINDOW_W ||
        y < wy + 36 || y >= wy + WINDOW_H - BROWSER_VIEW_BOTTOM)
        return 0;
    if(y>=wy+36 && y<wy+BROWSER_VIEW_TOP) {
        if(x>=wx+8 && x<wx+36) { browser_address_editing=0; browser_go_back(); }
        else if(x>=wx+42 && x<wx+70) { browser_address_editing=0; browser_go_forward(); }
        // This explicitly gives the browser keyboard focus when the address bar is clicked.
        else if (x >= wx + 78 && x < wx + WINDOW_W - 10) {
            window_focus(browser_window_id);

            if (!browser_address_editing) {
                browser_address_editing = 1;
                browser_address_clear();
            }
        }
        window_request_redraw();
        return 1;
    }
    /* Toolbar remains usable without a loaded document. */
    if (browser_document == NULL)
        return 0;
    browser_address_editing=0;
    for (unsigned i = 0; i < browser_link_count; i++) {
        browser_link_hit_t *hit = &browser_links[i];
        if (x < hit->x || x >= hit->x + hit->w ||
            y < hit->y || y >= hit->y + hit->h || !hit->href)
            continue;
        char url[256];
        size_t count = hit->href_length;
        if (count >= sizeof(url)) count = sizeof(url) - 1;
        for (size_t j = 0; j < count; j++) url[j] = (char)hit->href[j];
        url[count] = '\0';
        console_print("Browser link: ");
        console_print(url);
        console_print("\n");
        browser_queue_url(url);
        return 1;
    }
    return 0;
}

int browser_keyboard_event(int key)
{
    if (!browser_address_editing) return 0;

    if (key == '\r' || key == '\n') {
        browser_address_editing = 0;
        if (browser_address_len) {
            /* Explicit schemes are passed unchanged; bare domains get HTTP.
             * HTTPS is never silently downgraded to HTTP. */
            if ((browser_address_len >= 7 &&
                 browser_address[0]=='h' && browser_address[1]=='t' &&
                 browser_address[2]=='t' && browser_address[3]=='p' &&
                 browser_address[4]==':' && browser_address[5]=='/' &&
                 browser_address[6]=='/') ||
                (browser_address_len >= 8 &&
                 browser_address[0]=='h' && browser_address[1]=='t' &&
                 browser_address[2]=='t' && browser_address[3]=='p' &&
                 browser_address[4]=='s' && browser_address[5]==':' &&
                 browser_address[6]=='/' && browser_address[7]=='/')) {
                browser_queue_url(browser_address);
            } else {
                /* A different scheme must be rejected, not prefixed. */
                int other_scheme=0;
                for (unsigned i=0;i<browser_address_len;i++) {
                    if (browser_address[i]=='/' || browser_address[i]=='?') break;
                    if (browser_address[i]==':') { other_scheme=1; break; }
                }
                if (other_scheme) {
                    console_print("Browser: unsupported address scheme\n");
                } else if (browser_address_len+7 < ADDRESS_CAP) {
                    char url[ADDRESS_CAP];
                    const char prefix[]="http://";
                    unsigned j=0;
                    for (unsigned i=0;prefix[i];i++) url[j++]=prefix[i];
                    for (unsigned i=0;i<browser_address_len;i++)
                        url[j++]=browser_address[i];
                    url[j]=0;
                    browser_queue_url(url);
                } else console_print("Browser: URL too long\n");
            }
        }
        window_request_redraw();
        return 1;
    }
    if (key == '\b' || key == 127) {
        if (browser_address_len)
            browser_address[--browser_address_len]=0;
    } else if (key >= 32 && key <= 126 &&
               browser_address_len+1 < ADDRESS_CAP) {
        browser_address[browser_address_len++]=(char)key;
        browser_address[browser_address_len]=0;
    }
    window_request_redraw();
    return 1;
}
