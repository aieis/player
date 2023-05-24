GSTINC = $(shell pkg-config --cflags gstreamer-1.0)
GSTLIB = $(shell pkg-config --libs gstreamer-1.0 gstreamer-app-1.0)

INCS = $(GSTINC) -Iinclude
LIBS = $(GSTLIB)

CC = gcc -g
LD = gcc -g
CFLAGS=-pedantic -Wall -Os $(INCS)
LDFLAGS=$(LIBS)

SRCDIR	= src
OBJDIR	= obj
TARGET	= lamina

SRCEXT	= .c
OBJEXT	= .o

SRCTREE	= $(shell find $(SRCDIR) -type d)
SRCS	= $(shell find $(SRCDIR) -type f -name '*$(SRCEXT)')
OBJTREE	= $(foreach D,$(SRCTREE),$(shell echo $(D) | sed 's/$(SRCDIR)/$(OBJDIR)/'))
OBJSTMP	= $(foreach F,$(SRCS),$(shell echo $(F) | sed -e 's/$(SRCDIR)/$(OBJDIR)/'))
OBJS	= $(foreach O,$(OBJSTMP),$(shell echo $(O) | sed -e 's/\$(SRCEXT)/\$(OBJEXT)/'))

all: $(TARGET)	
	@echo Done.

run: $(TARGET)
	@./$(TARGET)

clean:
	@rm -r $(TARGET) $(OBJS) $(OBJDIR)

$(TARGET): $(OBJS)
	@$(LD) -o $@ $^ $(LDFLAGS)

$(OBJS): $(OBJDIR)/%$(OBJEXT) : $(SRCDIR)/%$(SRCEXT) | $(OBJDIR)
	@$(CC) -c -o $@ $? $(CFLAGS)

$(OBJDIR):
	@mkdir -p $(OBJDIR)
