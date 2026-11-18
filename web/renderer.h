#ifndef WEB_RENDERER_H
#define WEB_RENDERER_H

#include <lexbor/html/html.h>

/* Browser viewport input. Returns 1 when the browser handles it. */
int browser_mouse_wheel(int x, int y, int wheel);
int browser_mouse_click(int x, int y);
/* Handle printable keys and Enter while the address bar is active. */
int browser_keyboard_event(int key);

void html_render_at(lxb_html_document_t *document, int window_x, int window_y);
void html_render(lxb_html_document_t *document);
void browser_window_draw(void *context);
void browser_open(lxb_html_document_t *document);
/* Releases the active document and its CSS; leaves the browser window alive. */
void browser_discard_document(void);

/*
 * Reopen the existing browser window without creating
 * or parsing another HTML document.
 *
 * Returns 1 if a document is available, otherwise 0.
 */
int browser_reopen(void);

#endif
