#ifndef WEB_NAVIGATION_H
#define WEB_NAVIGATION_H
int browser_queue_url(const char *href);
int browser_navigation_poll(void);
const char *browser_current_url(void);
int browser_secure_page(void);
int browser_can_back(void);
int browser_can_forward(void);
int browser_go_back(void);
int browser_go_forward(void);
/* Called when htmltest creates its built-in local page. */
void browser_reset_to_local_page(void);
#endif
