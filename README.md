# Vulkan Examples

This repo contains a collection of example vulkan programs. The objective is for them to be as easy
to understand and to copy-paste from as possible. As such, they are designed to have as few
external dependences as possible, and as far as is reasonable, to be straight line code that can be
read from top to bottom. As a result, they do not follow good design principles of breaking down
code into smaller reusable functions since this obfuscates the examples. Similarly they are very
suboptimal as a result of favouring simplicity over performance. Therefore whilst they aim to be
examples of "working" vulkan code, they do not aim to be examples of "good" vulkan code.

The "offscreen" programs are generally the simplest. They setup vulkan, perform a single operation
to generate an image, save that image to a ppm file, and close. The "onscreen" are similar to the
offscreen versions except that instead of doing the operation once and saving the image to a file,
they instead have a loop where the image is repeatedly rendered to images in a swapchain that are
displayed in a window. The "anim" versions of the "onscreen" programs are typically the same as the
non-anim "onscreen" programs with a little extra complexity added to allow the image to change
between frames.
