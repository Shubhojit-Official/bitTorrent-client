##############################################################
#				    Editable Configs					     #
##############################################################

CC     = gcc
CFLAGS = -Wall -g
LDFLAGS = -lws2_32          # linker flags, applied AFTER object files

EXEC_NAME = app
OBJDIR = obj
SRCDIR = src
BINDIR = bin
BIN = $(BINDIR)/$(EXEC_NAME)

###########################################################################
#		           Do NOT edit beyond this Line						      #
###########################################################################

SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))

all: $(BIN)

# Linking — LDFLAGS must come AFTER object files
$(BIN): $(OBJS)
	@echo "Building Objects...."
	@echo "Objects to be linked are: " $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Built Successfully"

# Compiling — LDFLAGS not needed here at all
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@echo "Compiling source files..."
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Deleting old exec and objects"
	rm -r $(BINDIR)/* $(OBJDIR)/*
	@echo "Done!"