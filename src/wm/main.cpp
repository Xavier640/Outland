#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <unistd.h>
#include <sys/wait.h>
#include <algorithm>
#include <cstdio>

Display* dpy;
int screen;
Window root;

struct Node {
    Window win = None;
    Node* left = nullptr;
    Node* right = nullptr;
    Node* parent = nullptr;

    bool is_leaf() const { return left == nullptr && right == nullptr; }
};

Node* root_node = nullptr;
Window current_focus = None;

#define MOD Mod1Mask
#define COLOR_FOCUS   0x55ffff  
#define COLOR_UNFOCUS 0x222222  

XWindowAttributes start_attr;
XButtonEvent start_mouse;

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

Node* find_node(Node* node, Window w) {
    if (!node) return nullptr;
    if (node->is_leaf() && node->win == w) return node;
    
    Node* left_res = find_node(node->left, w);
    if (left_res) return left_res;
    
    return find_node(node->right, w);
}

Node* get_any_leaf(Node* node) {
    if (!node) return nullptr;
    if (node->is_leaf()) return node;
    return get_any_leaf(node->left);
}

void set_focus(Window w) {
    if (w == None) return;
    current_focus = w;
    XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
    XRaiseWindow(dpy, w);
}

void insert_window(Window w) {
    if (!root_node) {
        root_node = new Node{w, nullptr, nullptr, nullptr};
        return;
    }

    Node* target = find_node(root_node, current_focus);
    if (!target) target = get_any_leaf(root_node);

    Window old_win = target->win;
    target->win = None;

    target->left = new Node{old_win, nullptr, nullptr, target};
    target->right = new Node{w, nullptr, nullptr, target};
}

void remove_window(Window w) {
    Node* node = find_node(root_node, w);
    if (!node) return;

    if (node == root_node) {
        delete root_node;
        root_node = nullptr;
        current_focus = None;
        return;
    }

    Node* parent = node->parent;
    Node* sibling = (parent->left == node) ? parent->right : parent->left;

    parent->win = sibling->win;
    parent->left = sibling->left;
    parent->right = sibling->right;

    if (parent->left) parent->left->parent = parent;
    if (parent->right) parent->right->parent = parent;

    delete node;
    delete sibling;

    Node* next_focus = get_any_leaf(root_node);
    if (next_focus) set_focus(next_focus->win);
}

void tile_tree(Node* node, int x, int y, int w, int h) {
    if (!node) return;

    if (node->is_leaf()) {
        XMoveWindow(dpy, node->win, x, y);
        resize_window(node->win, w - 4, h - 4);
        
        if (node->win == current_focus) {
            XSetWindowBorder(dpy, node->win, COLOR_FOCUS);
        } else {
            XSetWindowBorder(dpy, node->win, COLOR_UNFOCUS);
        }
        return;
    }

    if (w >= h) {
        int w1 = w / 2;
        int w2 = w - w1;
        tile_tree(node->left, x, y, w1, h);
        tile_tree(node->right, x + w1, y, w2, h);
    } else {
        int h1 = h / 2;
        int h2 = h - h1;
        tile_tree(node->left, x, y, w, h1);
        tile_tree(node->right, x, y + h1, w, h2);
    }
}

void apply_layout() {
    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);
    tile_tree(root_node, 0, 0, sw, sh);
}

void grab_buttons(Window w) {
    XGrabButton(dpy, Button1, AnyModifier, w, True,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeSync, GrabModeAsync, None, None);
}

void manage_window(Window w) {
    XSelectInput(dpy, w, FocusChangeMask | StructureNotifyMask);
    XSetWindowBorderWidth(dpy, w, 2);
    
    grab_buttons(w);
    XMapWindow(dpy, w);

    insert_window(w);
    set_focus(w);
    apply_layout();
}

void unmanage_window(Window w) {
    remove_window(w);
    apply_layout();
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
                Window target = ev.xbutton.window;
                if (target != None && target != root) {
                    set_focus(target);
                    apply_layout();
                }
                XAllowEvents(dpy, ReplayPointer, CurrentTime);
                break;
            }

            case KeyPress: {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);

                if (ev.xkey.state & MOD) {
                    if (ks == XK_Return) {
                        const char* cmd[] = { "xterm", nullptr };
                        spawn(cmd);
                    } else if (ks == XK_C && (ev.xkey.state & ShiftMask)) {
                        if (current_focus != None) {
                            close_window(current_focus);
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