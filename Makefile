GLFWINC = $(shell pkg-config --cflags glfw3) 
GLFWLIB = $(shell pkg-config --libs glfw3)
GLIBINC = $(shell pkg-config --cflags glib-2.0) 
GLIBLIB = $(shell pkg-config --libs glib-2.0)
GSTINC = $(shell pkg-config --cflags gstreamer-1.0)
GSTLIB = $(shell pkg-config --libs gstreamer-1.0 gstreamer-app-1.0)
GTKINC =  $(shell pkg-config --cflags gtk+-2.0)
GTKLIB =  $(shell pkg-config --libs gtk+-2.0)
AVINC = $(shell pkg-config --cflags libavcodec libavdevice libavfilter libavformat libavcodec libswresample libswscale libavutil)	
AVLIB = $(shell pkg-config --libs libavcodec libavdevice libavfilter libavformat libavcodec libswresample libswscale libavutil)
EXINC = $(shell pkg-config --cflags vulkan)
EXLIB = $(shell pkg-config --libs vulkan)

INCS = $(GLFWINC) $(GLIBINC) $(GSTINC) $(AVINC) $(EXINC) -I./extern/imgui -I./extern/implot -I./extern/readerwriterqueue -I./extern/argparse
LIBS = $(GLFWLIB) $(GLIBLIB) $(GSTLIB) $(AVLIB) $(EXLIB)

CC = gcc -Os -g
LD = gcc -Os -g
CFLAGS= $(INCS) -pedantic -Wall -std=c++17
LDFLAGS=$(LIBS) -lm -lGL -lstdc++

SRCDIR	= src
OBJDIR	= obj
TARGET	= rrvp

HEADEXT = .h
SRCEXT	= .cpp
OBJEXT	= .o

SRCTREE	= $(shell find $(SRCDIR) -type d)
SRCS	= $(shell find $(SRCDIR) -type f -name '*$(SRCEXT)')
HEADERS	= $(shell find $(SRCDIR) -type f -name '*$(HEADEXT)')
OBJTREE	= $(foreach D,$(SRCTREE),$(shell echo $(D) | sed 's/$(SRCDIR)/$(OBJDIR)/'))
OBJSTMP	= $(foreach F,$(SRCS),$(shell echo $(F) | sed -e 's/$(SRCDIR)/$(OBJDIR)/'))
OBJS	= $(foreach O,$(OBJSTMP),$(shell echo $(O) | sed -e 's/\$(SRCEXT)/\$(OBJEXT)/'))

all: $(TARGET)
	@echo Done.

run: $(TARGET)
	./$(TARGET)

clean:
	rm -r $(TARGET) $(OBJS) $(OBJDIR)

$(TARGET): $(OBJS)
	$(LD) -o $@ $^ $(LDFLAGS)

$(OBJS): $(OBJDIR)/%$(OBJEXT) : $(SRCDIR)/%$(SRCEXT) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $?

$(OBJDIR):
	mkdir -p $(OBJDIR)
