/*
 * dik.h - DirectInput keyboard scan codes, as the game stores them in its
 * key bindings and names them in the key setup menu ("DIK 00" at
 * 0x4931bc and the names that follow).
 */

#ifndef IGN_DIK_H
#define IGN_DIK_H

enum {
    DIK_ESCAPE = 0x01, DIK_1 = 0x02, DIK_2 = 0x03, DIK_3 = 0x04, DIK_4 = 0x05,
    DIK_5 = 0x06, DIK_6 = 0x07, DIK_7 = 0x08, DIK_8 = 0x09, DIK_9 = 0x0a,
    DIK_0 = 0x0b, DIK_MINUS = 0x0c, DIK_EQUALS = 0x0d, DIK_BACK = 0x0e,
    DIK_TAB = 0x0f, DIK_Q = 0x10, DIK_W = 0x11, DIK_E = 0x12, DIK_R = 0x13,
    DIK_T = 0x14, DIK_Y = 0x15, DIK_U = 0x16, DIK_I = 0x17, DIK_O = 0x18,
    DIK_P = 0x19, DIK_LBRACKET = 0x1a, DIK_RBRACKET = 0x1b, DIK_RETURN = 0x1c,
    DIK_LCONTROL = 0x1d, DIK_A = 0x1e, DIK_S = 0x1f, DIK_D = 0x20, DIK_F = 0x21,
    DIK_G = 0x22, DIK_H = 0x23, DIK_J = 0x24, DIK_K = 0x25, DIK_L = 0x26,
    DIK_SEMICOLON = 0x27, DIK_APOSTROPHE = 0x28, DIK_GRAVE = 0x29,
    DIK_LSHIFT = 0x2a, DIK_BACKSLASH = 0x2b, DIK_Z = 0x2c, DIK_X = 0x2d,
    DIK_C = 0x2e, DIK_V = 0x2f, DIK_B = 0x30, DIK_N = 0x31, DIK_M = 0x32,
    DIK_COMMA = 0x33, DIK_PERIOD = 0x34, DIK_SLASH = 0x35, DIK_RSHIFT = 0x36,
    DIK_MULTIPLY = 0x37, DIK_LMENU = 0x38, DIK_SPACE = 0x39, DIK_CAPITAL = 0x3a,
    DIK_F1 = 0x3b, DIK_F2 = 0x3c, DIK_F3 = 0x3d, DIK_F4 = 0x3e, DIK_F5 = 0x3f,
    DIK_F6 = 0x40, DIK_F7 = 0x41, DIK_F8 = 0x42, DIK_F9 = 0x43, DIK_F10 = 0x44,
    DIK_NUMLOCK = 0x45, DIK_SCROLL = 0x46, DIK_NUMPAD7 = 0x47, DIK_NUMPAD8 = 0x48,
    DIK_NUMPAD9 = 0x49, DIK_SUBTRACT = 0x4a, DIK_NUMPAD4 = 0x4b,
    DIK_NUMPAD5 = 0x4c, DIK_NUMPAD6 = 0x4d, DIK_ADD = 0x4e, DIK_NUMPAD1 = 0x4f,
    DIK_NUMPAD2 = 0x50, DIK_NUMPAD3 = 0x51, DIK_NUMPAD0 = 0x52,
    DIK_DECIMAL = 0x53, DIK_F11 = 0x57, DIK_F12 = 0x58,
    DIK_NUMPADENTER = 0x9c, DIK_RCONTROL = 0x9d, DIK_DIVIDE = 0xb5,
    DIK_RMENU = 0xb8, DIK_HOME = 0xc7, DIK_UP = 0xc8, DIK_PRIOR = 0xc9,
    DIK_LEFT = 0xcb, DIK_RIGHT = 0xcd, DIK_END = 0xcf, DIK_DOWN = 0xd0,
    DIK_NEXT = 0xd1, DIK_INSERT = 0xd2, DIK_DELETE = 0xd3, DIK_LWIN = 0xdb
};

#endif /* IGN_DIK_H */
