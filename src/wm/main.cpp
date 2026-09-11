#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include <algorithm>
#include <cstdio>

Display* dpy;
int screen;
Window root;

struct ManagedWindow {
    Window win;
};

std::vector<ManagedWindow> windows;
int focus_index = -1;

#define MOD Mod1Mask
#define COLOR_FOCUS   0x55ffff  
#define COLOR_UNFOCUS 0x222222  

XWindowAttributes start_attr;
XButtonEvent start_mouse;

void tile() {
    int n = (int)windows.size();
    if (n == 0) return;

    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);

    if (n == 1) {
        XMoveWindow(dpy, windows[0].win, 0, 0);
        XResizeWindow(dpy, windows[0].win, sw - 4, sh - 4); 
    } else {
        int master_w = sw / 2;
        XMoveWindow(dpy, windows[0].win, 0, 0);
        XResizeWindow(dpy, windows[0].win, master_w - 4, sh - 4);

        int stack_w = sw - master_w;
        int stack_h = sh / (n - 1);

        for (int i = 1; i < n; i++) {
            int x = master_w;
            int y = (i - 1) * stack_h;
            int h = (i == n - 1) ? (sh - y) : stack_h;

            XMoveWindow(dpy, windows[i].win, x, y);
            XResizeWindow(dpy, windows[i].win, stack_w - 4, h - 4);
        }
    }
}

int x_error_handler(Display *dpy, XErrorEvent *ee) {
    return 0;
}

void spawn(const char* cmd[]) {
    if (fork() == 0) {
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execvp(cmd[0], (char* const*)cmd);
        _exit(1);
    }
}

void focus_window(int idx) {
    if (idx < 0 || idx >= (int)windows.size()) return;

    for (int i = 0; i < (int)windows.size(); i++) {
        if (i == idx) {
            XSetWindowBorder(dpy, windows[i].win, COLOR_FOCUS);
            XSetInputFocus(dpy, windows[i].win, RevertToParent, CurrentTime);
            XRaiseWindow(dpy, windows[i].win);
        } else {
            XSetWindowBorder(dpy, windows[i].win, COLOR_UNFOCUS);
        }
    }
    focus_index = idx;
}

void resize_window(Window w, int width, int height) {
    XSizeHints hints;
    long supplied;
    if (XGetWMNormalHints(dpy, w, &hints, &supplied)) {
        if (hints.flags & PMinSize) {
            width = std::max(width, hints.min_width);
            height = std::max(height, hints.min_height);
        }
        if (hints.flags & PResizeInc) {
            int base_w = (hints.flags & PBaseSize) ? hints.base_width : 0;
            int base_h = (hints.flags & PBaseSize) ? hints.base_height : 0;
            if (hints.width_inc > 0)
                width -= (width - base_w) % hints.width_inc;
            if (hints.height_inc > 0)
                height -= (height - base_h) % hints.height_inc;
        }
    }

    width = std::max(50, width);
    height = std::max(50, height);

    XResizeWindow(dpy, w, width, height);

    XWindowAttributes attr;
    XGetWindowAttributes(dpy, w, &attr);

    XConfigureEvent ce = {};
    ce.type = ConfigureNotify;
    ce.display = dpy;
    ce.event = w;
    ce.window = w;
    ce.x = attr.x;
    ce.y = attr.y;
    ce.width = width;
    ce.height = height;
    ce.border_width = attr.border_width;
    ce.above = None;
    ce.override_redirect = False;

    XSendEvent(dpy, w, False, StructureNotifyMask, (XEvent *)&ce);
}

void grab_buttons(Window w) {
    XGrabButton(dpy, Button1, MOD, w, True,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);

    XGrabButton(dpy, Button3, MOD, w, True,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
}

void manage_window(Window w) {
    XSelectInput(dpy, w, FocusChangeMask | StructureNotifyMask);
    XSetWindowBorderWidth(dpy, w, 2);
    
    grab_buttons(w);

    XMapWindow(dpy, w);
    windows.push_back({w});
    
    tile();
    focus_window((int)windows.size() - 1);
}

void unmanage_window(Window w) {
    auto it = std::find_if(windows.begin(), windows.end(), [&](const ManagedWindow& mw) {
        return mw.win == w;
    });

    if (it == windows.end()) return;

    windows.erase(it);
    if (focus_index >= (int)windows.size()) focus_index = (int)windows.size() - 1;
    
    tile();
    if (focus_index >= 0) focus_window(focus_index);
}

void close_window(Window w) {
    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XEvent xev = {};
    xev.xclient.type = ClientMessage;
    xev.xclient.window = w;
    xev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = (long)wm_delete;
    xev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, w, False, NoEventMask, &xev);
}

void grab_keys() {
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Return), MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_C), MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Tab), MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Q), MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
}

int main() {
    XSetErrorHandler(x_error_handler);

    dpy = XOpenDisplay(nullptr);
    if (!dpy) return 1;

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);
    grab_keys();

    signal(SIGCHLD, SIG_IGN);

    bool running = true;
    XEvent ev;

    while (running) {
        XNextEvent(dpy, &ev);

        switch (ev.type) {
            case MapRequest:
                manage_window(ev.xmaprequest.window);
                break;

            case DestroyNotify:
                unmanage_window(ev.xdestroywindow.window);
                break;

            case ConfigureRequest: {
                XWindowChanges changes;
                changes.x = ev.xconfigurerequest.x;
                changes.y = ev.xconfigurerequest.y;
                changes.width = ev.xconfigurerequest.width;
                changes.height = ev.xconfigurerequest.height;
                changes.border_width = 2;
                changes.sibling = ev.xconfigurerequest.above;
                changes.stack_mode = ev.xconfigurerequest.detail;
                XConfigureWindow(dpy, ev.xconfigurerequest.window, ev.xconfigurerequest.value_mask, &changes);
                break;
            }

           case ButtonPress: {
                // Vizăm fereastra principală (ev.xbutton.window), nu sub-elementele din ea
                Window target = ev.xbutton.window;
                if (target != None && target != root) {
                    XGetWindowAttributes(dpy, target, &start_attr);
                    start_mouse = ev.xbutton;
                    start_mouse.window = target; // Salvăm fereastra principală
                    
                    // Căutăm indexul ferestrei pentru a-i da focus
                    for (size_t i = 0; i < windows.size(); i++) {
                        if (windows[i].win == target) {
                            focus_window((int)i);
                            break;
                        }
                    }
                }
                break;
            }

            case MotionNotify: {
                if (start_mouse.window != None) {
                    int xdiff = ev.xbutton.x_root - start_mouse.x_root;
                    int ydiff = ev.xbutton.y_root - start_mouse.y_root;

                    if (start_mouse.button == Button1) {
                        // Alt + Click Stânga Drag -> Mutare fereastră principală
                        XMoveWindow(dpy, start_mouse.window,
                                    start_attr.x + xdiff,
                                    start_attr.y + ydiff);
                    } else if (start_mouse.button == Button3) {
                        // Alt + Click Dreapta Drag -> Redimensionare fereastră principală
                        int new_w = start_attr.width + xdiff;
                        int new_h = start_attr.height + ydiff;
                        resize_window(start_mouse.window, new_w, new_h);
                    }
                }
                break;
            }

            case ButtonRelease: {
                start_mouse.window = None;
                break;
            }

            case KeyPress: {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);

                if (ev.xkey.state & MOD) {
                    if (ks == XK_Return) {
                        const char* cmd[] = { "xterm", nullptr };
                        spawn(cmd);
                    } else if (ks == XK_C && (ev.xkey.state & ShiftMask)) {
                        if (focus_index >= 0 && focus_index < (int)windows.size()) {
                            close_window(windows[focus_index].win);
                        }
                    } else if (ks == XK_Tab) {
                        if (!windows.empty()) {
                            int next = (focus_index + 1) % windows.size();
                            focus_window(next);
                        }
                    } else if (ks == XK_Q && (ev.xkey.state & ShiftMask)) {
                        running = false;
                    }
                }
                break;
            }
        }
    }

    XCloseDisplay(dpy);
    return 0;
}