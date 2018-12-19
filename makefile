CC=clang++ -g -stdlib=libc++ -std=c++11
INC=-I./ffmpeg/include
LIB=-L./ffmpeg/lib -lswscale.5 -lavcodec.58 -lavdevice.58 -lavfilter.7 -lavformat.58 -lavutil.56 -lswresample.3 -lz \
	-lbz2.1.0.5 -llzma -lSystem.B  -lm 
FRAMEWORKS=-framework VideoToolbox -framework CoreFoundation -framework CoreGraphics -framework QuartzCore \
		   -framework AudioToolbox -framework CoreMedia -framework CoreVideo \
		   -framework CoreFoundation -framework VideoToolbox -framework CoreMedia -framework CoreVideo -Wl,-framework,CoreFoundation -Wl,-framework,Security  -framework CoreServices -framework CoreGraphics -framework VideoToolbox -framework CoreImage -framework AVFoundation -framework AudioToolbox -framework AppKit -framework OpenGL
SRCS=transcoding.cpp ConditionEvent.cpp transcode_helper.cpp
OBJS=$(SRCS:.cpp=.o)
EXEC=app
start:$(OBJS)
	$(CC) -o  $(EXEC) -g $(OBJS) $(LIB) $(FRAMEWORKS)
.cpp.o:
	$(CC) $(INC) -o $@ -g -c $<

clean:
	rm -rf $(OBJS)
cp:clean $(OBJS)
