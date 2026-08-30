#include <X11/Xlib.h>
#include <cstdio>
#include <X11/keysym.h>

int main() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) { fprintf(stderr, "Can't open display\n"); return 1; }

    int screen = DefaultScreen(dpy);
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen),
                                      100, 100, 200, 200, 1,
                                      BlackPixel(dpy, screen),
                                      WhitePixel(dpy, screen));

    XSelectInput(dpy, win, ExposureMask | KeyPressMask);
    XMapWindow(dpy, win);

    bool running = true;
    while (running) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym((XKeyEvent*)&ev, 0);
            if (ks == XK_q) running = false;
            else printf("key: %c\n", (char)ks);
        }
    }

    XCloseDisplay(dpy);
    return 0;
}