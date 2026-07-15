NAME = webserv

SRC_DIR   = src
INC_DIR   = include
BUILD_DIR = build

CC     = c++
CFLAGS = -std=c++98 -Wall -Wextra -Werror -I$(INC_DIR)

SRCS = main.cpp \
       Config.cpp \
       ConfigParser.cpp \
       ListenSocket.cpp \
       Connection.cpp \
       EventLoop.cpp \
#        HttpRequest.cpp \
#        HttpResponse.cpp \
#        Router.cpp \
#        RequestHandler.cpp \
#        CgiProcess.cpp

OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.cpp=.o))
DEPS = $(OBJS:.o=.d)

RM = rm -f

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	$(RM) -r $(BUILD_DIR)

fclean: clean
	$(RM) $(NAME)

re: fclean all

# デバッグビルド: LOG マクロ(common.hpp の #ifdef DEBUG)を有効化する。
# 提出時の通常ビルド(make / make all)には -DDEBUG が入らないので LOG は消える。
# 目的別にオブジェクトを作り分けないよう、必ずフルリビルド(re)経由にする。
debug: CFLAGS += -DDEBUG
debug: re

.PHONY: all clean fclean re debug