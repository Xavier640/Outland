#include <X11/Xlib.h>
#include <cstdio>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <vector>
#include <algorithm>

Display* dpy;
int screen;
Window root;

struct ManagedWindow {
    Window frame;
    Window client;
    bool focused;
};

std::vector<ManagedWindow> windows;
int focus_index = -1;

void focus_window(int idx){
    if (idx <0 || idx >= (int)windows.size()) return;
    for (int i = 0; i< (int)windows.size(); i++)
        windows[i].focused = (i == idx);
    focus_index = idx;
    XRaiseWindow(dpy, windows[idx].frame);
    XSetInputFocus(dpy, windows[idx].frame, RevertToParent, CurrentTime);
}

void close_window(int idx){
    if( idx < 0 || idx >= (int)windows.size()) return;

    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", false);
    XEvent xev = {};
    xev.xclient.type = ClientMessage;
    xev.xclient.window = windows[idx].client;
    xev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = (long)wm_delete;
    xev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, windows[idx].client, False, StructureNotifyMask, &xev);
    XSync(dpy, False);
}

void manage_window(Window client){
    XWindowAttributes attr;
    XGetWindowAttributes(dpy, client, &attr);

    int border = 2;
Window frame = XCreateSimpleWindow(dpy, root, attr.x - border, attr.y - border, 
                                       attr.width + border * 2, attr.height + border * 2, 
                                       border, BlackPixel(dpy, screen), WhitePixel(dpy, screen));

    XSelectInput(dpy, frame, StructureNotifyMask | SubstructureNotifyMask);
    XMapWindow(dpy, frame);

    XReparentWindow(dpy, client, frame, border, border);

    windows.push_back({frame, client, false});
    focus_window((int)windows.size() - 1);
}

void unmanage_window(Window client){
    auto it = std::find_if(windows.begin(), windows.end(), [&](const ManagedWindow& w) {return w.client == client; });
    if (it == windows.end()) return;

    XDestroyWindow(dpy, it->frame);
    windows.erase(it);

    if(focus_index >= (int)windows.size()) focus_index = windows.size() -1;
    if (focus_index >= 0) focus_window(focus_index);
}

int main() {
    dpy = XOpenDisplay(nullptr);
    if (!dpy) { fprintf(stderr, "Failed to open display\n"); return 1; }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    XSelectInput(dpy, root, SubstructureRedirectMask || SubstructureNotifyMask);

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
            
            case ConfigureRequest:
                XWindowChanges changes;
                changes.x = ev.xconfigurerequest.x;
                changes.y = ev.xconfigurerequest.y;
                changes.width = ev.xconfigurerequest.width;
                changes.height = ev.xconfigurerequest.height;
                changes.border_width = ev.xconfigurerequest.border_width;
                changes.sibling = ev.xconfigurerequest.above;
                changes.stack_mode = ev.xconfigurerequest.detail;
                XConfigureWindow(dpy, ev.xconfigurerequest.window, ev.xconfigurerequest.value_mask, &changes);
                break;

            case KeyPress:
                KeySym ks = XLookupKeysym((XKeyEvent*)&ev, 0);
                if (ks == XK_q) running = false;
                break;


            
        }
    }

    XCloseDisplay(dpy);
    return 0;
}