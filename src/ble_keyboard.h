const short NOKEY=88;
const short CAPS=128;
const short DISPLAY4080=256;
; // note: 512 bit is unused
const short RESTORE=1024;
const short DOSHIFT=2048; // force left shift pressed
const short NOSHIFT=4096; // force left and right shift released
const short DOCBM=8192; // force commodore key pressed

class BleKeyboard
{
public:
    void begin();
    void press(int scancode);
    void release(int scancode);
    void releaseAll();
    bool isConnected();
};