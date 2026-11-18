#ifndef WEB_CSS_H
#define WEB_CSS_H

#include <stdint.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>

/*
 * Basic computed CSS properties for Tiny64.
 *
 * Text color is inherited from the parent.
 * Background color is not inherited.
 *
 * Padding is the space inside an element's box.
 * Margin is the space outside its box.
 *
 * All spacing values are stored as pixels.
 */
typedef struct {
    uint32_t color;
    uint32_t background_color;
    int has_background;

    int padding_top;
    int padding_right;
    int padding_bottom;
    int padding_left;

    int margin_top;
    int margin_right;
    int margin_bottom;
    int margin_left;

    /* Phase 3: small pixel-based layout and typography subset. */
    int font_px;             /* 0 means inherit the current font size */
    int width_px;            /* -1 means automatic width */
    int height_px;           /* -1 means automatic height */
    int margin_left_auto;
    int margin_right_auto;
    int display_none;
    int text_align;          /* 0 left, 1 center, 2 right */
    int border_width;        /* uniform solid border, pixels */
    uint32_t border_color;
} css_computed_style_t;

/*
 * Apply matching stylesheet rules to an element.
 *
 * The caller initializes the style with its
 * inherited/default values before calling.
 */
void css_compute_style(
    lxb_dom_node_t *node,
    css_computed_style_t *style
);

/*
 * Parse CSS from <style> elements in an HTML document.
 *
 * This replaces the previously cached stylesheet.
 *
 * Returns 1 on success, otherwise 0.
 */
int css_load_document(lxb_html_document_t *document);

/*
 * Return the currently cached stylesheet.
 *
 * Returns NULL if no stylesheet is loaded.
 */
lxb_css_stylesheet_t *css_get_stylesheet(void);

/*
 * Release the cached stylesheet and its parser.
 */
void css_unload_document(void);

/*
 * Print the parsed CSS rule tree for debugging.
 *
 * Does not modify rendering.
 */
void css_debug_rules(void);

#endif