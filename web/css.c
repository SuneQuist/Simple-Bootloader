#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <lexbor/css/rule.h>
#include <lexbor/css/base.h>
#include <lexbor/css/css.h>
#include <lexbor/css/parser.h>
#include <lexbor/css/stylesheet.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/dom/interfaces/character_data.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/css/property.h>
#include <lexbor/css/property/const.h>
#include <lexbor/css/value.h>
#include <lexbor/css/value/const.h>
#include <lexbor/css/unit/const.h>
#include <lexbor/css/selectors/selector.h>
#include <lexbor/tag/tag.h>

#include "../console/console.h"
#include "../memory/heap.h"

#include "css.h"

/*
 * Cached CSS objects.
 *
 * The stylesheet is parsed once when the browser
 * opens, then reused during rendering.
 */
static lxb_css_parser_t *cached_parser = NULL;
static lxb_css_stylesheet_t *cached_sheet = NULL;

/*
 * Print a size_t value without depending on printf.
 */
static void css_print_number(size_t value)
{
    char buffer[32];
    size_t i = sizeof(buffer) - 1;

    buffer[i] = '\0';

    do {
        buffer[--i] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);

    console_print(&buffer[i]);
}

/*
 * Show the available heap memory.
 */
static void css_print_heap(void)
{
    console_print("CSS: free heap = ");
    css_print_number(heap_free_bytes());
    console_print(" bytes\n");

    console_print("CSS: largest free block = ");
    css_print_number(heap_largest_free_block());
    console_print(" bytes\n");

    console_print("CSS: largest failed allocation = ");
    css_print_number(heap_largest_failed_request());
    console_print(" bytes\n");
}

/*
 * Walk Lexbor's parsed CSS rule tree.
 *
 * Count parsed style rules for diagnostics.
 */
static unsigned css_count_style_rules(
    lxb_css_rule_t *rule,
    unsigned depth
)
{
    if (rule == NULL || depth >= 32)
        return 0;

    unsigned count = 0;

    /*
     * Lexbor stores the stylesheet's children
     * inside a rule-list node.
     */
    if (rule->type == LXB_CSS_RULE_STYLESHEET ||
        rule->type == LXB_CSS_RULE_LIST)
    {
        lxb_css_rule_list_t *list =
            lxb_css_rule_list(rule);

        for (lxb_css_rule_t *child = list->first;
             child != NULL;
             child = child->next)
        {
            count += css_count_style_rules(child, depth + 1);
        }

        return count;
    }

    if (rule->type == LXB_CSS_RULE_STYLE) {
        count++;

        /*
         * Nested rules may appear inside a style rule.
         */
        lxb_css_rule_style_t *style =
            lxb_css_rule_style(rule);

        if (style->child != NULL) {
            count += css_count_style_rules(
                (lxb_css_rule_t *)style->child,
                depth + 1
            );
        }
    }

    return count;
}

/*
 * Compare two byte strings exactly.
 *
 * CSS class names and IDs are case-sensitive.
 */
static int css_bytes_equal(
    const lxb_char_t *a, size_t a_len,
    const lxb_char_t *b, size_t b_len
)
{
    if (a == NULL || b == NULL || a_len != b_len)
        return 0;

    for (size_t i = 0; i < a_len; ++i) {
        if (a[i] != b[i])
            return 0;
    }

    return 1;
}

/*
 * Check whether a whitespace-separated class
 * attribute contains the requested class name.
 *
 * Example:
 *     class="card featured"
 *
 * Matches:
 *     .card
 *     .featured
 *
 * Does not match:
 *     .car
 */
static int css_class_contains(
    const lxb_char_t *classes, size_t classes_len,
    const lxb_char_t *wanted, size_t wanted_len
)
{
    if (classes == NULL || wanted == NULL || wanted_len == 0)
        return 0;

    size_t i = 0;

    while (i < classes_len) {
        while (i < classes_len &&
               (classes[i] == ' ' || classes[i] == '\t' ||
                classes[i] == '\n' || classes[i] == '\r' ||
                classes[i] == '\f'))
        {
            i++;
        }

        size_t start = i;

        while (i < classes_len &&
               classes[i] != ' ' && classes[i] != '\t' &&
               classes[i] != '\n' && classes[i] != '\r' &&
               classes[i] != '\f')
        {
            i++;
        }

        if (css_bytes_equal(
                classes + start, i - start,
                wanted, wanted_len))
        {
            return 1;
        }
    }

    return 0;
}

/*
 * Match a compound selector against one DOM element.
 *
 * Supported components: element, class, ID.
 * Examples: div.card, p.notice.important, div#main.card.
 *
 * Complex selectors (descendants, child combinators,
 * pseudo-classes and attributes) are deliberately rejected.
 * We do not want to silently style the wrong element.
 */
static int css_selector_matches(
    lxb_css_selector_list_t *list,
    lxb_dom_node_t *node
)
{
    if (list == NULL || node == NULL ||
        node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
        list->first == NULL)
        return 0;

    lxb_dom_element_t *element = lxb_dom_interface_element(node);
    if (element == NULL)
        return 0;

    for (lxb_css_selector_t *selector = list->first;
         selector != NULL; selector = selector->next)
    {
        if (selector->type == LXB_CSS_SELECTOR_TYPE_ELEMENT) {
            size_t length = 0;
            const lxb_char_t *tag =
                lxb_dom_element_local_name(element, &length);
            if (!css_bytes_equal(selector->name.data,
                                 selector->name.length, tag, length))
                return 0;
        }
        else if (selector->type == LXB_CSS_SELECTOR_TYPE_CLASS) {
            size_t length = 0;
            const lxb_char_t *classes =
                lxb_dom_element_get_attribute(element,
                    (const lxb_char_t *)"class", 5, &length);
            if (!css_class_contains(classes, length,
                                    selector->name.data,
                                    selector->name.length))
                return 0;
        }
        else if (selector->type == LXB_CSS_SELECTOR_TYPE_ID) {
            size_t length = 0;
            const lxb_char_t *id =
                lxb_dom_element_get_attribute(element,
                    (const lxb_char_t *)"id", 2, &length);
            if (!css_bytes_equal(id, length,
                                 selector->name.data,
                                 selector->name.length))
                return 0;
        }
        else {
            /* Unknown or unsupported selector component. */
            return 0;
        }
    }
    return 1;
}

/*
 * Specificity is compared lexicographically: IDs,
 * classes, then element names. Keep source order
 * for ties. These bounds exceed our practical DOM
 * selector length in Tiny64.
 */
static unsigned css_selector_specificity(lxb_css_selector_list_t *list)
{
    unsigned ids = 0, classes = 0, elements = 0;
    for (lxb_css_selector_t *s = list->first; s != NULL; s = s->next) {
        if (s->type == LXB_CSS_SELECTOR_TYPE_ID) ids++;
        else if (s->type == LXB_CSS_SELECTOR_TYPE_CLASS) classes++;
        else if (s->type == LXB_CSS_SELECTOR_TYPE_ELEMENT) elements++;
    }
    if (ids > 255) ids = 255;
    if (classes > 255) classes = 255;
    if (elements > 255) elements = 255;
    return (ids << 16) | (classes << 8) | elements;
}

/*
 * Convert a Lexbor hexadecimal CSS color into
 * Tiny64's 0x00RRGGBB framebuffer format.
 *
 * Alpha blending is not implemented yet.
 */
static int css_hex_to_rgb(const lxb_css_value_color_t *value,
                          uint32_t *rgb)
{
    if (value == NULL || rgb == NULL)
        return 0;

    if (value->type != LXB_CSS_VALUE_HEX)
        return 0;

    *rgb =
        ((uint32_t)value->u.hex.rgba.r << 16) |
        ((uint32_t)value->u.hex.rgba.g << 8) |
        (uint32_t)value->u.hex.rgba.b;

    return 1;
}

/*
 * Forward declarations for CSS length conversion.
 *
 * The implementations are located further down
 * in this file.
 */
static int css_length_to_px(
    const lxb_css_value_length_percentage_t *value,
    int *pixels
);

static int css_padding_to_px(
    const lxb_css_value_length_percentage_t *value,
    int *pixels
);

/*
 * Apply supported color and spacing declarations from a matching rule.
 */
static void css_apply_declarations(
    lxb_css_rule_declaration_list_t *list,
    css_computed_style_t *computed
)
{
    if (list == NULL || computed == NULL)
        return;

    for (lxb_css_rule_t *rule = list->first;
         rule != NULL;
         rule = rule->next)
    {
        if (rule->type != LXB_CSS_RULE_DECLARATION)
            continue;

        lxb_css_rule_declaration_t *decl =
            (lxb_css_rule_declaration_t *)rule;

        uint32_t rgb;

        /* Display: none hides the entire subtree. */
        if (decl->type == LXB_CSS_PROPERTY_DISPLAY && decl->u.display != NULL) {
            computed->display_none =
                (decl->u.display->a == LXB_CSS_DISPLAY_NONE);
        }
        /* Font size: pixel values only; the framebuffer font scales in integers. */
        else if (decl->type == LXB_CSS_PROPERTY_FONT_SIZE &&
                 decl->u.font_size != NULL &&
                 decl->u.font_size->type == LXB_CSS_FONT_SIZE__LENGTH) {
            const lxb_css_value_length_percentage_t *v =
                &decl->u.font_size->length;
            int px = 0;
            if (css_length_to_px(v, &px) && px >= 8 && px <= 64)
                computed->font_px = px;
        }
        else if (decl->type == LXB_CSS_PROPERTY_WIDTH &&
                 css_length_to_px(decl->u.width, &computed->width_px)) {
            if (computed->width_px < 0) computed->width_px = -1;
        }
        else if (decl->type == LXB_CSS_PROPERTY_HEIGHT &&
                 css_length_to_px(decl->u.height, &computed->height_px)) {
            if (computed->height_px < 0) computed->height_px = -1;
        }
        else if (decl->type == LXB_CSS_PROPERTY_TEXT_ALIGN &&
                 decl->u.text_align != NULL) {
            unsigned a = decl->u.text_align->type;
            if (a == LXB_CSS_TEXT_ALIGN_CENTER) computed->text_align = 1;
            else if (a == LXB_CSS_TEXT_ALIGN_RIGHT || a == LXB_CSS_TEXT_ALIGN_END)
                computed->text_align = 2;
            else if (a == LXB_CSS_TEXT_ALIGN_LEFT || a == LXB_CSS_TEXT_ALIGN_START)
                computed->text_align = 0;
        }
        else if (decl->type == LXB_CSS_PROPERTY_BORDER && decl->u.border != NULL) {
            lxb_css_property_border_t *b = decl->u.border;
            int px = 0;
            if (b->style == LXB_CSS_BORDER_SOLID &&
                b->width.type == LXB_CSS_VALUE__LENGTH &&
                (int)b->width.length.unit == (int)LXB_CSS_UNIT_PX) {
                double n = b->width.length.num;
                if (n >= 1 && n <= 8) px = (int)n;
            }
            if (px > 0) {
                computed->border_width = px;
                if (!css_hex_to_rgb(&b->color, &computed->border_color))
                    computed->border_color = computed->color;
            }
        }
        else if (decl->type == LXB_CSS_PROPERTY_COLOR) {
            if (css_hex_to_rgb(decl->u.color, &rgb))
                computed->color = rgb;
        }
        else if (decl->type ==
                 LXB_CSS_PROPERTY_BACKGROUND_COLOR)
        {
            if (css_hex_to_rgb(decl->u.background_color, &rgb)) {
                computed->background_color = rgb;
                computed->has_background = 1;
            }
        }
        /*
        * CSS padding shorthand:
        *
        *     padding: 20px;
        *     padding: 10px 20px;
        *
        * Lexbor has already expanded the shorthand
        * into top, right, bottom, and left values.
        */
        else if (decl->type == LXB_CSS_PROPERTY_PADDING &&
                decl->u.padding != NULL)
        {
            lxb_css_property_padding_t *padding = decl->u.padding;

            css_padding_to_px(&padding->top, &computed->padding_top);
            css_padding_to_px(&padding->right, &computed->padding_right);
            css_padding_to_px(&padding->bottom, &computed->padding_bottom);
            css_padding_to_px(&padding->left, &computed->padding_left);
        }

        /*
        * Individual padding properties.
        */
        else if (decl->type == LXB_CSS_PROPERTY_PADDING_TOP) {
            css_padding_to_px(
                decl->u.padding_top,
                &computed->padding_top
            );
        }
        else if (decl->type == LXB_CSS_PROPERTY_PADDING_RIGHT) {
            css_padding_to_px(
                decl->u.padding_right,
                &computed->padding_right
            );
        }
        else if (decl->type == LXB_CSS_PROPERTY_PADDING_BOTTOM) {
            css_padding_to_px(
                decl->u.padding_bottom,
                &computed->padding_bottom
            );
        }
        else if (decl->type == LXB_CSS_PROPERTY_PADDING_LEFT) {
            css_padding_to_px(
                decl->u.padding_left,
                &computed->padding_left
            );
        }

        /*
        * CSS margin shorthand.
        *
        * Unlike padding, margins may be negative.
        */
        else if (decl->type == LXB_CSS_PROPERTY_MARGIN &&
                decl->u.margin != NULL)
        {
            lxb_css_property_margin_t *margin = decl->u.margin;

            css_length_to_px(&margin->top, &computed->margin_top);
            css_length_to_px(&margin->right, &computed->margin_right);
            css_length_to_px(&margin->bottom, &computed->margin_bottom);
            css_length_to_px(&margin->left, &computed->margin_left);
            computed->margin_left_auto = (margin->left.type == LXB_CSS_VALUE_AUTO);
            computed->margin_right_auto = (margin->right.type == LXB_CSS_VALUE_AUTO);
        }

        /*
        * Individual margin properties.
        */
        else if (decl->type == LXB_CSS_PROPERTY_MARGIN_TOP) {
            css_length_to_px(
                decl->u.margin_top,
                &computed->margin_top
            );
        }
        else if (decl->type == LXB_CSS_PROPERTY_MARGIN_RIGHT) {
            computed->margin_right_auto =
                decl->u.margin_right != NULL &&
                decl->u.margin_right->type == LXB_CSS_VALUE_AUTO;
            css_length_to_px(
                decl->u.margin_right,
                &computed->margin_right
            );
        }
        else if (decl->type == LXB_CSS_PROPERTY_MARGIN_BOTTOM) {
            css_length_to_px(
                decl->u.margin_bottom,
                &computed->margin_bottom
            );
        }
        else if (decl->type == LXB_CSS_PROPERTY_MARGIN_LEFT) {
            computed->margin_left_auto =
                decl->u.margin_left != NULL &&
                decl->u.margin_left->type == LXB_CSS_VALUE_AUTO;
            css_length_to_px(
                decl->u.margin_left,
                &computed->margin_left
            );
        }
    }
}

/*
 * Gather matching rules without allocating from the heap.
 * A rule with multiple matching selectors takes the highest
 * specificity of those selectors.
 *
 * We sort the rules by specificity before applying them.
 * This preserves source order on equal specificity and lets
 * higher-specificity declarations override lower-specificity
 * declarations regardless of their order in the stylesheet.
 *
 * Limit: at most 128 matching rules per element.
 */
#define CSS_MAX_MATCHED_RULES 128

typedef struct {
    lxb_css_rule_declaration_list_t *declarations;
    unsigned specificity;
} css_matched_rule_t;

static void css_collect_rules(lxb_css_rule_t *rule,
                              lxb_dom_node_t *node,
                              css_matched_rule_t *matches,
                              unsigned *count,
                              unsigned depth)
{
    if (rule == NULL || depth >= 32)
        return;

    if (rule->type == LXB_CSS_RULE_STYLESHEET ||
        rule->type == LXB_CSS_RULE_LIST)
    {
        lxb_css_rule_list_t *list = lxb_css_rule_list(rule);
        for (lxb_css_rule_t *child = list->first;
             child != NULL; child = child->next)
            css_collect_rules(child, node, matches, count, depth + 1);
        return;
    }

    if (rule->type != LXB_CSS_RULE_STYLE)
        return;

    lxb_css_rule_style_t *style = lxb_css_rule_style(rule);
    unsigned best = 0;
    int found = 0;
    for (lxb_css_selector_list_t *s = style->selector;
         s != NULL; s = s->next)
    {
        if (css_selector_matches(s, node)) {
            unsigned specificity = css_selector_specificity(s);
            if (!found || specificity > best) best = specificity;
            found = 1;
        }
    }
    if (!found || *count >= CSS_MAX_MATCHED_RULES)
        return;

    /* Stable insertion sort; equal scores stay in source order. */
    unsigned at = *count;
    while (at > 0 && matches[at - 1].specificity > best) {
        matches[at] = matches[at - 1];
        at--;
    }
    matches[at].declarations = style->declarations;
    matches[at].specificity = best;
    (*count)++;
}

static void css_apply_rules(lxb_css_rule_t *root,
                            lxb_dom_node_t *node,
                            css_computed_style_t *computed)
{
    css_matched_rule_t matches[CSS_MAX_MATCHED_RULES];
    unsigned count = 0;
    css_collect_rules(root, node, matches, &count, 0);
    for (unsigned i = 0; i < count; i++)
        css_apply_declarations(matches[i].declarations, computed);
}

/*
 * Convert a Lexbor CSS length into Tiny64 pixels.
 *
 * Supported:
 *     0
 *     12px
 *     20px
 *
 * Unsupported for now:
 *     percentages, em, rem, auto, and other units.
 *
 * Returns 1 when the value can be converted.
 */
static int css_length_to_px(
    const lxb_css_value_length_percentage_t *value,
    int *pixels
)
{
    if (value == NULL || pixels == NULL)
        return 0;

    if (value->type != LXB_CSS_VALUE__LENGTH)
        return 0;

    /*
    * Lexbor's unit field and unit constants use
    * distinct enum types in this version.
    *
    * Compare their numeric representations.
    */
    if ((int)value->u.length.unit != (int)LXB_CSS_UNIT_PX &&
        value->u.length.num != 0.0)
    {
        return 0;
    }

    double number = value->u.length.num;

    /*
     * Keep layout dimensions within a safe
     * range for Tiny64's integer coordinates.
     */
    if (number < -32768.0 || number > 32767.0)
        return 0;

    *pixels = (int)number;
    return 1;
}

/*
 * Convert CSS padding to a nonnegative
 * integer pixel value.
 */
static int css_padding_to_px(
    const lxb_css_value_length_percentage_t *value,
    int *pixels
)
{
    int result;

    if (!css_length_to_px(value, &result))
        return 0;

    if (result < 0)
        return 0;

    *pixels = result;
    return 1;
}


/*
 * Compute the basic CSS style for a DOM element.
 *
 * The caller provides the initial inherited color.
 * Background color is reset because it is not inherited.
 */
void css_compute_style(lxb_dom_node_t *node,
                       css_computed_style_t *computed)
{
    if (node == NULL || computed == NULL)
        return;

    computed->has_background = 0;
    computed->background_color = 0;
    /*
    * CSS padding and margins are not inherited.
    */
    computed->padding_top = 0;
    computed->padding_right = 0;
    computed->padding_bottom = 0;
    computed->padding_left = 0;

    computed->margin_top = 0;
    computed->margin_right = 0;
    computed->margin_bottom = 0;
    computed->margin_left = 0;
    computed->margin_left_auto = 0;
    computed->margin_right_auto = 0;
    computed->width_px = -1;
    computed->height_px = -1;
    computed->display_none = 0;
    computed->border_width = 0;
    computed->border_color = 0;
    /* font_px and text_align inherit from the caller. */

    lxb_css_stylesheet_t *sheet = css_get_stylesheet();

    if (sheet == NULL || sheet->root == NULL)
        return;

    css_apply_rules(sheet->root, node, computed);
}

/*
 * Print how many style rules Lexbor parsed.
 */
void css_debug_rules(void)
{
    lxb_css_stylesheet_t *sheet = css_get_stylesheet();

    if (sheet == NULL || sheet->root == NULL) {
        console_print("CSS: no parsed rules\n");
        return;
    }

    unsigned count =
        css_count_style_rules(sheet->root, 0);

    if (count == 0)
        console_print("CSS: no style rules found\n");
    else if (count == 1)
        console_print("CSS: found 1 style rule\n");
    else if (count == 2)
        console_print("CSS: found 2 style rules\n");
    else
        console_print("CSS: found multiple style rules\n");
}

/*
 * Release the previously cached CSS objects.
 */
void css_unload_document(void)
{
    if (cached_parser != NULL) {
        lxb_css_parser_destroy(cached_parser, true);
        cached_parser = NULL;
    }

    if (cached_sheet != NULL) {
        lxb_css_stylesheet_destroy(cached_sheet, true);
        cached_sheet = NULL;
    }
}

/*
 * Return the currently parsed stylesheet.
 */
lxb_css_stylesheet_t *css_get_stylesheet(void)
{
    return cached_sheet;
}

/*
 * Parse one CSS text buffer.
 *
 * Diagnostic version: report the exact stage
 * that fails while parsing embedded CSS.
 */
static int css_parse_text(const lxb_char_t *data, size_t length)
{
    if (data == NULL || length == 0) {
        console_print("CSS: empty input\n");
        return 1;
    }

    console_print("CSS: creating parser\n");

    if (cached_parser == NULL) {
        cached_parser = lxb_css_parser_create();

        if (cached_parser == NULL) {
            console_print("CSS: parser creation FAILED\n");
            return 0;
        }

        lxb_status_t status =
            lxb_css_parser_init(cached_parser, NULL);

        if (status != LXB_STATUS_OK) {
            console_print("CSS: parser initialization FAILED\n");
            return 0;
        }
    }

    console_print("CSS: parser initialized\n");

    if (cached_sheet != NULL) {
        lxb_css_stylesheet_destroy(cached_sheet, true);
        cached_sheet = NULL;
    }

    console_print("CSS: creating stylesheet\n");

    /*
    * Create a smaller CSS memory pool for Tiny64.
    *
    * Lexbor's default stylesheet pool prepares 1024
    * objects, which consumes too much of our 512 KiB heap.
    *
    * Start with 64 objects instead.
    */
    lxb_css_memory_t *memory = lxb_css_memory_create();

    if (memory == NULL) {
        console_print("CSS: memory creation FAILED\n");
        return 0;
    }

    /*
    * Check available memory before initializing
    * Lexbor's CSS memory pools.
    */
    console_print("CSS: heap before pool initialization\n");
    css_print_heap();

    lxb_status_t memory_status =
        lxb_css_memory_init(memory, 64);

    if (memory_status != LXB_STATUS_OK) {
        console_print("CSS: memory initialization FAILED\n");

        /*
        * Inspect the heap immediately after failure.
        */
        css_print_heap();

        lxb_css_memory_destroy(memory, true);
        return 0;
    }

    /*
    * Create the stylesheet using our smaller pool.
    */
    cached_sheet = lxb_css_stylesheet_create(memory);

    if (cached_sheet == NULL) {
        console_print("CSS: stylesheet creation FAILED\n");
        lxb_css_memory_destroy(memory, true);
        return 0;
    }

    /*
    * The stylesheet now holds its own reference
    * to the memory pool.
    *
    * Release our original reference.
    */
    lxb_css_memory_ref_dec_destroy(memory);

    console_print("CSS: stylesheet created OK\n");

    console_print("CSS: parsing stylesheet\n");

    lxb_status_t status = lxb_css_stylesheet_parse(
        cached_sheet,
        cached_parser,
        data,
        length
    );

    if (status != LXB_STATUS_OK) {
        console_print("CSS: stylesheet parse FAILED\n");
        return 0;
    }

    if (cached_sheet->root == NULL) {
        console_print("CSS: stylesheet root is NULL\n");
        return 0;
    }

    console_print("CSS: stylesheet parsed OK\n");
    return 1;
}

/*
 * Find and parse CSS from <style> elements.
 */
static int css_scan_node(lxb_dom_node_t *node, unsigned depth)
{
    if (node == NULL || depth >= 32)
        return 1;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT &&
        node->local_name == LXB_TAG_STYLE)
    {
        for (lxb_dom_node_t *child = node->first_child;
             child != NULL;
             child = child->next)
        {
            if (child->type != LXB_DOM_NODE_TYPE_TEXT)
                continue;

            lxb_dom_character_data_t *text =
                lxb_dom_interface_character_data(child);

            if (text != NULL && text->data.data != NULL) {
                console_print("CSS: found style text\n");

                if (text->data.length == 0) {
                    console_print("CSS: style text is empty\n");
                    continue;
                }

                if (!css_parse_text(
                        text->data.data,
                        text->data.length))
                {
                    console_print("CSS: parsing style text FAILED\n");
                    return 0;
                }

                console_print("CSS: parsing style text OK\n");
            }
        }
    }

    for (lxb_dom_node_t *child = node->first_child;
         child != NULL;
         child = child->next)
    {
        if (!css_scan_node(child, depth + 1))
            return 0;
    }

    return 1;
}

/*
 * Extract and parse embedded CSS from the document.
 */
/*
 * Extract and parse embedded CSS from the document.
 */
int css_load_document(lxb_html_document_t *document)
{
    css_unload_document();

    if (document == NULL) {
        console_print("CSS: document is NULL\n");
        return 0;
    }

    console_print("CSS: scanning document\n");

    if (document->head != NULL) {
        console_print("CSS: scanning head\n");

        if (!css_scan_node(
                (lxb_dom_node_t *)document->head, 0))
        {
            console_print("CSS: head scan failed\n");
            css_unload_document();
            return 0;
        }
    }
    else {
        console_print("CSS: head is NULL\n");
    }

    if (document->body != NULL) {
        console_print("CSS: scanning body\n");

        if (!css_scan_node(
                (lxb_dom_node_t *)document->body, 0))
        {
            console_print("CSS: body scan failed\n");
            css_unload_document();
            return 0;
        }
    }

    if (cached_sheet == NULL) {
        console_print("CSS: no stylesheet found\n");
        return 0;
    }

    console_print("CSS: stylesheet parsed OK!\n");
    return 1;
}