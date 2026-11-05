#ifndef BUTTON_H
#define BUTTON_H

typedef void (*ButtonCallback)(); // hàm callback, không nhận gì, không trả gì

void initButton(ButtonCallback onPress);

#endif // BUTTON_H