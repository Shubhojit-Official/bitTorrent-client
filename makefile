##############################################################
#					    Editable Configs					 #
##############################################################

CC = gcc
CFLAGS = -Wall -g

EXEC_NAME = app
OBJDIR = obj
SRCDIR = src
BINDIR = bin
BIN = $(BINDIR)/$(EXEC_NAME)



###########################################################################
#		           Do NOT edit beyond this Line							  #
###########################################################################

SRCS = $(wildcard $(SRCDIR)/*.c) # list of src files
OBJS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS)) #list of object files

all: $(BIN)

#Linking the object files and creating a executable
$(BIN): $(OBJS)
	@echo "Building Objects...."
	@echo "Objects to be linked are: " $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ 
	@echo "Built Successfully"

#Compiling the source files from the src/ dir
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@echo "Compiling source files..."
	$(CC) $(CFLAGS) -c $< -o $@

# Removes execs from bin/ and objects from obj/
clean:
	@echo "Deleting old exec and objects"
	rm -r $(BINDIR)/* $(OBJDIR)/*
	@echo "Done!"