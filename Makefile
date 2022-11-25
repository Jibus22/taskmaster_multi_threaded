NAME := taskmaster

### DIRECTORIES ###
SRC_DIRECTORY := ./src
INC_DIRECTORY := ./include
INC_DIRECTORY2 := ./src
LIB_DIRECTORY := ./lib
BUILD_DIRECTORY := ./build
LIB_FT_DIR =	./libft
TEST_DIRECTORY := ./test
CONFIG_DIRECTORY := $(TEST_DIRECTORY)/config
SCRIPT_DIRECTORY := $(TEST_DIRECTORY)/scripts
SRC_TEST_DIRECTORY := $(TEST_DIRECTORY)/srcs

### YAML ###
YAML_SRC := ./yaml-0.2.5
YAMLPACKAGE := $(YAML_SRC).tar.gz
YAML := $(LIB_DIRECTORY)/libyaml-0.so.2.0.9

### SOURCE ###
SRC := $(shell find $(SRC_DIRECTORY) -name '*.c')
OBJ := $(SRC:$(SRC_DIRECTORY)/%.c=$(BUILD_DIRECTORY)/%.o)
DEPS := $(OBJ:.o=.d)

### COMPILATION ###
CC := clang
INC_FLAGS := $(addprefix -I,$(INC_DIRECTORY) $(INC_DIRECTORY2))
CPPFLAGS := $(INC_FLAGS) -D_GNU_SOURCE -MMD -MP
CFLAGS := -Werror -fcommon

### LINK ###
LDFLAGS := -L$(LIB_DIRECTORY)
LDLIBS := -lyaml -ltermcap -pthread


### RULES ###
all: CPPFLAGS += -DDEVELOPEMENT #make alone compile in dev mode
all: $(YAML) $(NAME)

prod: CPPFLAGS += -DPRODUCTION
prod: $(YAML) $(NAME)

debug: CPPFLAGS += -DDEVELOPEMENT
debug: CFLAGS := -Wall -Wextra -g -O0 -gdwarf-4 -fcommon
debug: $(YAML) $(NAME)

san: CPPFLAGS += -DDEVELOPEMENT
san: CFLAGS := -g -O1\
	-fsanitize=address -fno-omit-frame-pointer \
	-fsanitize=leak \
	-fsanitize=pointer-compare \
	-fsanitize=pointer-subtract \
	-fsanitize=undefined
san: $(YAML) $(NAME)

test: CPPFLAGS += -DDEVELOPEMENT
test: $(YAML) $(NAME)
	@bash $(SCRIPT_DIRECTORY)/build_many_daemons.sh
	@./$(NAME) $(CONFIG_DIRECTORY)/config_3.yaml

retest: CPPFLAGS += -DDEVELOPEMENT
retest: $(YAML) $(NAME)
	@$(MAKE) -sC $(SRC_TEST_DIRECTORY) fclean
	@$(MAKE) -s test

kill:
	@bash $(SCRIPT_DIRECTORY)/shutdown_all_daemons.sh

$(YAML):
	@wget -c http://pyyaml.org/download/libyaml/$(YAMLPACKAGE)
	@tar -xf $(YAMLPACKAGE)
	@rm -Rf $(YAMLPACKAGE)
	@cd $(YAML_SRC) && \
		./configure \
		--prefix=$(PWD)/$(YAML_SRC)/yaml \
		--includedir=$(PWD)/$(INC_DIRECTORY) \
		--libdir=$(PWD)/$(LIB_DIRECTORY)
	@$(MAKE) -s -C $(YAML_SRC)
	@$(MAKE) -s -C $(YAML_SRC) install
	# @rm -Rf $(YAML_SRC)

$(NAME): $(OBJ)
	@echo $(call OPTIONS, $(B_WHITE))
	@echo "$(GREEN)  BUILD$(RESET)    $(H_WHITE)$@$(RESET)"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(BUILD_DIRECTORY)/%.o: $(SRC_DIRECTORY)/%.c
	@mkdir -p $(@D)
	@echo "$(GREEN)  CC$(RESET)       $<"
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	@echo "$(RED)  RM$(RESET)       $(BUILD_DIRECTORY)"
	@rm -rf $(BUILD_DIRECTORY)

fclean: clean
	@echo "$(RED)  RM$(RESET)       $(NAME)"
	@rm -f $(NAME)

re: fclean all

help:
	@echo $(call HELP,$(GREEN), $(call OPTIONS,  $(YELLOW))) 


.PHONY: all options clean fclean re debug prod san
-include $(DEPS)


### DOC ###
OPTIONS =\
		" $(1)CC$(RESET)       $(CC)\n"\
		"$(1)CFLAGS$(RESET)   $(CFLAGS)\n"\
		"$(1)CPPFLAGS$(RESET) $(CPPFLAGS)\n"\
		"$(1)LDFLAGS$(RESET)  $(LDFLAGS)\n"\
		"$(1)LDLIBS$(RESET)   $(LDLIBS)\n"

HELP =\
		"$(1) ------------------$(RESET)\n"\
		"usage : make [options] [target] ...\n"\
		"targets :\n"\
		"  prod:  production mode. add PRODUCTION macro\n"\
		"  debug: debug mode. add DEVELOPEMENT macro and '-g' to CFLAGS\n"\
		"  san:   fsanitize mode. add DEVELOPEMENT macro, '-g'\n"\
		"         and fsanitize options to CFLAGS\n\n"\
		"  test:  build testing daemons and run $(NAME)\n"\
		"  retest:rebuild testing daemons and run $(NAME)\n"\
		"  clean/fclean/re: you know, babe\n"\
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
