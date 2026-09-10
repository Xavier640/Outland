#include <X11/Xlib.h>
#include <cstdio>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>

Display* dpy;
int screen;
Window root;

struct ManagedWindow {
    Window win;
};

std::vector<ManagedWindow> windows;
int focus_index = -1;

#define MOD Mod1Mask

#define COLOR_FOCUS 0x55ffff
#define COLOR_UNFOCUS 0x222222

void spawn(const char* cmd[]){
    if (fork() == 0) {
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execvp(cmd[0], (char* const*)cmd);
        fprintf(stderr, "Error: execvp failed\n");
        _exit(1);
    }
}

void focus_window(int idx) {
    if (idx < 0 || idx >= (int)windows.size()) return;

    for (int i = 0; i < (int)windows.size(); i++) {
        if (i == idx) {
            XSetWindowBorder(dpy, windows[i].win, COLOR_FOCUS);
            XSetInputFocus(dpy, windows[i].win, RevertToParent, CurrentTime);
        } else {
            XSetWindowBorder(dpy, windows[i].win, COLOR_UNFOCUS);
        }
        }
    }
    focus_index = idx;
}

void manage_window(Window w) {
    XSelectInput(dpy, w, FocusChangeMask | StructureNotifyMask);
    XSetWindowBorderWidth(dpy, w, 2);

    windows.push_back({w});
    focus_window((int)windows.size() - 1);
}

void unmanage_window(Window w) {
    auto it = std::find_if(windows.begin(), windows.end(), [&](const ManagedWindow& mw) {
        return mw.win == w;
    });

    if (it == windows.end()) return;

    windows.erase(it);
    if (focus_index >= (int) windows.size()) focus_index = (int)windows.size() - 1;
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
    xev.sclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, w, False, NoEventMask, &xev);
}

void grab_keys() {
    XGrabKey(dpy, XKeySimToKeyCode(dpy, XK_Return), MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeySimToKeyCode(dpy, XK_C), MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeySimToKeyCode(dpy, XK_Tab), MOD, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeySimToKeyCode(dpy, XK_Q), MOD | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
}

int main() {
    
}