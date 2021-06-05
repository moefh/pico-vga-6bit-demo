# pico-vga-6bit-demo

This is a simple demo of VGA output with the Raspberry Pi Pico:

<img src="photos/screen.jpg" width="640" alt="Demo Screen" title="Demo Screen">

The [official repositories](https://github.com/raspberrypi/pico-playground) include demos with better VGA output, so you might look there first. I wrote this code because those examples don't work with my old CRT monitor (the monitor displays a very garbled screen and makes a scary noise when I try). The documentation mentions they use a "slightly non-standard 24Mhz system clock", which is almost 5% off the standard 25.175MHz, so maybe that's the reason.

This demo uses the a pixel clock of 25 MHz, which is much closer to the standard.  The resolution is 320x240 with just 6 bits for color information, which gives 64 colors in total. It has two framebuffers of 320x240 pixels, and anyone drawing to the frambuffer is responsible for correctly updating the 2 top bits which contain the sync signals (for vertical and horizontal sync).  Messing up those bits *will* result in the monitor losing synchronization, and can possibly damage the monitor (although I imagine modern LCD screens will just stop displaying the image and ignore the bad signal).

The basic design of the VGA signal generation code is based on bitluni's [ESP32Lib](https://github.com/bitluni/ESP32Lib), which generates VGA output with the ESP32 using the I2S peripheral.  This code is completely independent and uses the Pico's PIO to handle the output, but the organization of the DMA transfers draws heavily from VGALib's design.

To hook the monitor to the Pico, a resistor ladder or some other form of DAC is required for each color component (red, green and blue) to convert the digital output from the Pico to the analog color signal.  The V-Sync and H-Sync output pins require a resistor (220ohms should be OK) in series.

<img src="photos/board.jpg" width="640" alt="Pico board and VGA Connector" title="Pico board and VGA Connector">

Possible problems:

- I don't have a good solution for running a DMA chain cycle with no CPU support, so the code could mess the signal timing when the CPU gets too busy. The code tries to work around that problem by running the interrupt request handler that restarts the DMA chain on the second core, but that only works if the second core never gets too busy (which is the case in this demo, since everything else on it runs in the first core).


