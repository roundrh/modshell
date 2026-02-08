CC          := gcc
SRC_DIR     := src
INC_DIR     := include
OBJ_DIR     := obj

BASE_FLAGS  := -Wall -Werror -Wshadow -Wpedantic -Wwrite-strings -Wformat \
               -fstack-protector-strong

OPT_ONLY_FLAGS := -D_FORTIFY_SOURCE=2

INC_FLAGS   := -I$(INC_DIR)

NAME        := msh
NAME_DEV    := msh_dev
NAME_PROD   := msh_prod

SRCS        := $(shell find $(SRC_DIR) -name "*.c")

OBJS        := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/all/%.o)
OBJS_DEV    := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/dev/%.o)
OBJS_PROD   := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/prod/%.o)

all: $(NAME)
dev: $(NAME_DEV)
prod: $(NAME_PROD)

#all
$(NAME): $(OBJS)
	$(CC) $(BASE_FLAGS) $(OPT_ONLY_FLAGS) -fsanitize=address -fsanitize=undefined -O2 -g $(OBJS) -o $(NAME)

#dev
$(NAME_DEV): $(OBJS_DEV)
	$(CC) $(BASE_FLAGS) -g -O0 $(OBJS_DEV) -o $(NAME_DEV)

#prod
$(NAME_PROD): $(OBJS_PROD)
	$(CC) $(BASE_FLAGS) $(OPT_ONLY_FLAGS) -O3 $(OBJS_PROD) -o $(NAME_PROD)

$(OBJ_DIR)/all/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(INC_FLAGS) $(BASE_FLAGS) $(OPT_ONLY_FLAGS) -fsanitize=address -fsanitize=undefined -O2 -g -c $< -o $@

$(OBJ_DIR)/dev/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(INC_FLAGS) $(BASE_FLAGS) -g -O0 -c $< -o $@

$(OBJ_DIR)/prod/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(INC_FLAGS) $(BASE_FLAGS) $(OPT_ONLY_FLAGS) -O3 -c $< -o $@


#============ cleanup ===========#

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME) $(NAME_DEV) $(NAME_PROD)

re: fclean all

.PHONY: all dev prod clean fclean re
