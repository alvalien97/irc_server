NAME        = ircserv
CC          = c++
FLAGS       = -Wall -Wextra -Werror -std=c++98
INC_DIR     = inc
SRC_DIR     = src
OBJ_DIR     = obj

SRC         = $(SRC_DIR)/main.cpp \
              $(SRC_DIR)/Server.cpp \
              $(SRC_DIR)/Client.cpp \
              $(SRC_DIR)/Channel.cpp \
              $(SRC_DIR)/Bot.cpp \
			  $(SRC_DIR)/ServerUtils.cpp \
			  $(SRC_DIR)/ServerCommands.cpp \
			  $(SRC_DIR)/ServerChannelCommands.cpp

OBJ         = $(SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(FLAGS) $(OBJ) -o $(NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CC) $(FLAGS) -I$(INC_DIR) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
