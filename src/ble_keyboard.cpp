/*
    ble_keyboard.cpp

    BLE Commodore Keyboard Server
    for c-simple-emu-cbm (C Portable Version)
    by David R. Van Wagner davevw.com
    Changes are open source, MIT License
    (Based on ESP32 BLE Arduino : BLE_server)

    Original comments:
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleServer.cpp
    Ported to Arduino ESP32 by Evandro Copercini
    updates by chegewara
*/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#include "ble_keyboard.h"

// Commodore 64/128 BLE Keyboard Service
#define SERVICE_UUID "65da11f8-dc46-4cd6-bdc9-ba862c4634f5"

// Commodore 64/128 BLE Keyboard Scan Characteristic
#define CHARACTERISTIC_UUID "1652b589-a0cc-4319-87fd-d80ccbd668f0"

static BLECharacteristic *pCharacteristic;

static const auto max_pressed = 16;
static short scan_codes[max_pressed];
static bool _isConnected = false;
static const short scanmask = (RESTORE | DISPLAY4080 | CAPS | 127);

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      // Stage: Connected
      _isConnected = true;
      //Serial.println("Connected");
    };

    void onDisconnect(BLEServer* pServer) {
      // Stage: Disconnected
      _isConnected = false;
      //Serial.println("Disconnected");
      
      // Stage: Advertising (Restart so it can be found again)
      BLEDevice::startAdvertising();
    }
};

void BleKeyboard::begin()
{
    for (int i = 0; i < max_pressed; ++i)
        scan_codes[i] = NOKEY;

    BLEDevice::init("Palm Portable Keyboard Adapter for Commodore");

    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    pCharacteristic->setValue("");
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // functions that help with iPhone connections issue
    pAdvertising->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("Started BLE Keyboard Service");
    _isConnected = false;
}

void sendKeys()
{
    String keys = "";
    bool noshift = false;
    for (int i=0; !noshift && i<max_pressed && scan_codes[i] != NOKEY; ++i)
        if ((scan_codes[i] & NOSHIFT) != 0)
            noshift = true;
    for (int i=0; i<max_pressed && scan_codes[i] != NOKEY; ++i)
    {
        if (!noshift || ((scan_codes[i] & scanmask) != 15 && (scan_codes[i] & scanmask) != 52))
        {
            if (keys.length() > 0)
                keys = keys + ','; 
            keys += String(scan_codes[i] & scanmask);
        }
    }
    if (keys.length() == 0)
        keys = String(NOKEY);
    keys += '\n';
    Serial.println("Sending: " + keys);
    pCharacteristic->setValue(keys.c_str());
    pCharacteristic->notify();
}

void release(int scancode, bool send)
{
    if ((scancode == CAPS || scancode == DISPLAY4080) && send == true)
        return; // nothing to do on user release of soft switch (otherwise soft switch release [send=false] can reset)

    short found = -1;
    int i;
    for (i=0; i<max_pressed && scan_codes[i] != NOKEY; ++i)
    {
        if ((scan_codes[i] & scanmask) == (scancode & scanmask))
        {
            found = i;
            break;
        }
    }
    if (found < 0)
        return;

    bool unshift = (scan_codes[i] & DOSHIFT) != 0;
    bool uncbm = (scan_codes[i] & DOCBM) != 0;
    scan_codes[i] = NOKEY;

    if (i < max_pressed - 1)
    {
        for (int j = found; j < max_pressed - 1; ++j)
            scan_codes[j] = scan_codes[j + 1];
    }

    if (unshift)
    {
        release(15, false);
        release(52, false);
    }

    if (uncbm)
        release(61, false);

    if (send)
        sendKeys();
}

void BleKeyboard::release(int scancode)
{
    ::release(scancode, true);
}

void BleKeyboard::press(int scancode)
{
    short found = -1;
    int i;
    for (i=0; i<max_pressed && scan_codes[i] != NOKEY; ++i)
    {
        if ((scan_codes[i] & scanmask) == (scancode & scanmask))
        {
            found = i;
            break;
        }
    }
    if (found < 0)
    {
        if (i >= max_pressed)
            return;
        scan_codes[i] = scancode;
        if (scancode & DOSHIFT)
        {
            press(15);
            return;
        }
        if (scancode & DOCBM)
        {
            press(61);
            return;
        }
    }
    else
    {
        if (scancode == CAPS || scancode == DISPLAY4080)
        {
            ::release(scancode, false); // soft switch toggle off on user press
            return; // do not send
        }
    }
    sendKeys();
}

void BleKeyboard::releaseAll()
{
    bool caps = false;
    bool display4080 = false;
    for (int i=0; i<max_pressed; ++i)
    {
        // save toggles if found
        if (scan_codes[i] == CAPS)
            caps = true;
        if (scan_codes[i] == DISPLAY4080)
            display4080 = true;

        scan_codes[i] = NOKEY;
    }

    // restore toggles
    if (caps)
    {
        scan_codes[0] = CAPS;
        if (display4080)
            scan_codes[1] = DISPLAY4080;
    }
    else if (display4080)
        scan_codes[0] = DISPLAY4080;

    sendKeys();
}

bool BleKeyboard::isConnected()
{
    return _isConnected;
}