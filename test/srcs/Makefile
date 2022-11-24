### DIRECTORIES ###
SRC_DIRECTORY := .
INC_DIRECTORY := .
LIB_DIRECTORY := .
BUILD_DIRECTORY := $(SRC_DIRECTORY)/build
BIN_DIRECTORY := ../daemons

### SOURCE ###
SRC := $(shell find $(SRC_DIRECTORY) -name '*.c')
OBJ := $(SRC:$(SRC_DIRECTORY)/%.c=$(BUILD_DIRECTORY)/%${DAEMON_NAME}.o)
DEPS := $(OBJ:.o=.d)

### COMPILATION ###
CC := clang
INC_FLAGS := $(addprefix -I,$(INC_DIRECTORY))
CPPFLAGS := $(INC_FLAGS) -D_GNU_SOURCE -MMD -MP -DDAEMON_NAME=\"${DAEMON_NAME}\"
CFLAGS := #-Wextra -Wall -Werror

### LINK ###
LDFLAGS := -L$(LIB_DIRECTORY)
LDLIBS :=

NAME := $(BIN_DIRECTORY)/daemon_${DAEMON_NAME}

### RULES ###
all: $(NAME)

$(NAME): $(OBJ)
	@echo $(call OPTIONS, $(B_WHITE))
	@echo "$(GREEN)  BUILD$(RESET)    $(H_WHITE)$@$(RESET)"
	@$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(BUILD_DIRECTORY)/%${DAEMON_NAME}.o: $(SRC_DIRECTORY)/%.c
	@mkdir -p $(@D)
	@echo "$(GREEN)  CC$(RESET)       $<"
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	@echo "$(RED)  RM$(RESET)       $(BUILD_DIRECTORY)"
	@rm -rf $(BUILD_DIRECTORY)

fclean: clean
	@echo "$(RED)  RM$(RESET)       $(NAME)"
	@rm -f $(BIN_DIRECTORY)/*

re: fclean all

help:
	@echo $(call HELP,$(GREEN), $(call OPTIONS,  $(YELLOW))) 


.PHONY: all options clean fclean re debug prod san
-include $(DEPS)


### DOC ###
OPTIONS =\
		" $(1)CC$(RESET)       $(CC)\n"\
		"$(1)CPPFLAGS$(RESET) $(CPPFLAGS)\n"

HELP =\
		"$(1) ------------------$(RESET)\n"\
		"usage : make [options] [target] ...\n"\
		"targets :\n"\
		"  prod:  production mode. add PRODUCTION macro\n"\
		"  debug: debug mode. add DEVELOPEMENT macro and '-g' to CFLAGS\n"\
		"  san:   fsanitize mode. add DEVELOPEMENT macro, '-g'\n"\
		"         and fsanitize options to CFLAGS\n\n"\
		"Basic setup :\n "\
		$(2)\
		"$(1)-----------------$(RESET)"

### COLORS ###
BLUE = \e[0;34m
BLACK = \e[0;30m
GREEN = \e[0;32m
RED = \e[0;31m
CYAN = \e[0;36m
YELLOW = \e[0;33m
WHITE = \e[0;37m
H_WHITE = \e[0;97m
B_WHITE = \e[1;37m
BG_YELLOW = \e[43m
BG_BLACK = \e[40m
BG_CYAN = \e[46m
RESET = \e[0m
