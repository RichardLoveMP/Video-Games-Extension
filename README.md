# Video Games Extension
Extending a Video Game With Additional Graphical Features and a Serial Port Device

***Author: Tianzuo Qin***

***Time: Oct 14 2019***



#### Language Using

C++/C



Assembly x86



#### Device Using



TUX Controller with USB serial I/O



#### Project Detail:

##### Part 1: Ability Overview:

1. Write code that interacts directly with device
2. Abstract device with system software
3. Manipulate bits and to transform data from one format into another
4. Basic structure of event loop
5. Use mutex with pthread API

##### Part 2: Background Knowledge

***Device protocols:*** Write software that interacts directly with devices and must adhere to the protocols specified by those devices. Similar problems arise when one must meet software interface specifications, but you need experience with both in order to recognize the similarities and differences. Unfortunately, most of the devices accessible from within QEMU have fully developed drivers within Linux. The video card, however, is usually managed directly from user-level so as to improve performance, thus most of the code is in other software packages (such as XFree86). We are also fortunate to have a second device designed by Kevin Bassett and Mark Murphy, two previous staff members. The device is a game controller called the Tux Controller (look at the back of the board) that attaches to a USB port. You can find one on each of the machines in the lab. On the Tux Controller board is an FTDI “Virtual Com Port” (VCP) chip, which together with driver software in Windows makes the USB port appear to as an RS232 serial port. QEMU is then configured to map a QEMU-emulated serial port on the virtual machine to the VCP-emulated serial port connected to the Tux Controller. In this assignment, you will write code that interacts directly with both the (emulated) video card and the game controller board.





***Device abstraction:*** Most devices implement only part of the functionality that a typical user might associate with them. For example, disk drives provide only a simple interface through which bits can be stored and retrieved in fixed-size blocks of several kB. All other functionality, including everything from logical partitions and directories to variable-length files and file-sharing semantics, is supported by software, most of which resides in the operating system. In this project, you will abstract some of the functionality provided by the Tux controller board.





***Format interchange:*** This project gives you several opportunities for working with data layout in memory and for transforming data from one form to another. Most of these tasks relate to graphics, and involve taking bit vectors or pixel data laid out in a form convenient to C programmers and changing it into a form easily used by the Video Graphics Array (VGA) operating in mode X. Although the details of the VGA mode X layout are not particularly relevant today, they do represent the end product of good engineers working to push the limits on video technology. If you work with cutting-edge systems, you are likely to encounter situations in which data formats have been contorted for performance or to meet standards, and you are likely to have to develop systems to transform from one format to another.



***Event loops:*** The idea of an event loop is central to a wide range of software systems, ranging from video games and discrete event simulators to graphical user interfaces and web servers. An event loop is not much different than a state machine implemented in software, and structuring a software system around an event loop can help you to structure your thoughts and the design of the system. In this project, the event loop is already defined for you, but be sure to read it and understand how the implementation enables the integration of various activities and inputs in the game.





***Threading:*** Multiple threads of execution allow logically separate tasks to be executed using synchronous operations. If one thread blocks waiting for an operation to complete, other threads are still free to work. In this project, we illustrate the basic concepts by using a separate thread to clear away status messages after a fixed time has passed. You will need to synchronize your code in the main thread with this helper thread using a Posix mutex. You may want to read the class notes on Posix threads to help you understand these interactions. Later classes will assume knowledge of this material.

Software examples and test strategies: In addition to the five learning objectives for the assignment, this project provides you with examples of software structure as well as testing strategies for software components.

When you design software interfaces, you should do so in a way that allows you to test components separately in this manner and thus to deal with bugs as soon as possible and in as small a body of code as possible. Individual function tests and walking through each function in a debugger are also worthwhile, but hard to justify in an academic setting.

##### Part 3: Algorithm Usage in Color Clustering and VGA Palette Manipulation

***The Octree Algorithm:*** This section outlines the algorithm that you must use. The text here is simply a rewriting of Section 2.2 from http://www.leptonica.org/papers/colorquant.pdf, which is in turn mostly a practical overview of earlier work along with some useful implementation details. The paper does provide enough references for you to trace back to much of that earlier work (most of which you can find free online or by leveraging UIUC’s online library subscriptions). The paper also provides a number of more sophisticated algorithms, which you are wel- come to attempt or extend. As usual, we strongly recommend that you first get this version working and save a copy to avoid unhappiness at handin time.

The basic idea behind the algorithm is to set aside 64 colors for the 64 nodes of the second level of an octree, and then to use the remaining colors (in this case, 128 of them) to represent those nodes in the fourth level of an octree that contain the most pixels from the image. In other words, you choose the 128 colors that comprise as much of the image as possible and assign color values for them, then fill in the rest of the image with what’s left.



