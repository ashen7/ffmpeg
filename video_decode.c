#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "libavutil/avutil.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#define true 1

//bmp结构体 bmp存储方式是小端存储  即低字节放在低地址 高字节放在高地址
typedef struct {
	unsigned int bf_size;           //4 位图文件的大小
	unsigned short bf_reserved1;    //2 位图文件保留字，必须为0
	unsigned short bf_reserved2;    //2 位图文件保留字，必须为0
	unsigned int bf_offbits;        //4 位图数据的起始位置，以相对于位图文件头的偏移量表示，以字节为单位
} BitMapHeader;

typedef struct {
	unsigned int bi_size;		   //4 结构体所占用字节数
	int bi_width;				   //4 位图的宽度
	int bi_height;				   //4 位图的高度
	unsigned short bi_planes;	   //2 位图的平面数，必须为1
	unsigned short bi_bit_count;   //2 每个像素所需的位数，必须是1(双色), 4(16色)，8(256色)或24(真彩色)之一
	unsigned int bi_compression;   //4 位图压缩类型，必须是 0(不压缩),1(BI_RLE8压缩类型)或2(BI_RLE4压缩类型)之一
	unsigned int bi_size_image;    //4 实际位图数据占用的字节数
	int bi_xpixels_permeter;	   //4 位图x水平分辨率，每米像素数
	int bi_ypixels_permeter;	   //4 位图y垂直分辨率，每米像素数
	unsigned int bi_clr_used;	   //4 位图实际使用的颜色数
	unsigned int bi_clr_important; //4 位图显示过程中重要的颜色数
} BitMapInfoHeader;

const char* input_filename = "/home/yipeng/videos/2.mp4";//文件名
int video_stream_index = -1;							//视频流索引
size_t current_frame_number = 0;						//当前得到的帧数量
size_t frame_number = 0;								//总帧数
size_t pump_frame_rate = 3;								//抽帧率
size_t output_width = 640;								//输出rgb的宽
size_t output_height = 480;								//输出rgb的高

static int VideoDecode(AVFormatContext* avformat_context, AVCodecContext* avcodec_context);
static int EncodeYUVToJPG(const char* output_filename, AVFrame* avframe);
static int YUVToBGR24(const char* output_filename, AVFrame* avframe, AVCodecContext* avcodec_context);
static int BGR24ToBMP(const char* output_filename, uint8_t* bmp_data);

int VideoDecode(AVFormatContext* avformat_context, AVCodecContext* avcodec_context) {
	//保存的文件名
	char image_name[20];
	int send_result = -1;
	int receive_result = -1;

	while (true) {
		AVPacket avpacket;

		while (true) {
			av_init_packet(&avpacket);
			//5. 读取码流中的一帧视频 获得一帧视频的压缩数据AVPacket
			//H264中一个AVPacket对应一个NAL 
			if (av_read_frame(avformat_context, &avpacket) < 0) {
				printf("read frame fail\n");
				av_packet_unref(&avpacket);
				return -2;
			}

			//如果读出的包流索引 与 视频流索引 不相等
			if (video_stream_index != avpacket.stream_index) {
				//释放包
				av_packet_unref(&avpacket);
				continue;
			}
			else {
				break;
			}
		}

		//如果不是关键帧 退出
		/*if (avpacket.flags != AV_PKT_FLAG_KEY) {
		    av_packet_unref(&avpacket);
		    continue;
		}*/

		//10 发送包
		send_result = avcodec_send_packet(avcodec_context, &avpacket);
		if (0 != send_result
			&& AVERROR(EAGAIN) != send_result) {
			printf("send codec packet failed, send result: %d\n", send_result);
			av_packet_unref(&avpacket);
			return -1;
		}

		//11. 为帧分配内存 
		AVFrame* avframe = av_frame_alloc();
		if (NULL == avframe) {
			printf("alloc frame failed\n");
			av_packet_unref(&avpacket);
			return -1;
		}

		//12. 接收帧
		receive_result = avcodec_receive_frame(avcodec_context, avframe);
		if (0 != receive_result
			&& AVERROR(EAGAIN) != receive_result) {
			printf("receive codec frame failed, receive result: %d\n", receive_result);
			av_frame_free(&avframe);
			av_packet_unref(&avpacket);
			return -1;
		}

		if (0 != (++frame_number % pump_frame_rate)) {
			av_packet_unref(&avpacket);
			av_frame_free(&avframe);
		}
		else {
			sprintf(image_name, "%d.bmp", ++current_frame_number);
			/*avframe->pts = 0;
			avframe->quality = 10;

			if (0 != EncodeYUVToJPG(image_name, avframe)) {
				printf("encode jpg failed\n");
				av_packet_unref(&avpacket);
				//return -1;
			}*/
			YUVToBGR24(image_name, avframe, avcodec_context);
		}
	}

	return 0;
}

int EncodeYUVToJPG(const char* output_filename, AVFrame* avframe) {
	AVFormatContext* avformat_context = NULL;
	AVOutputFormat* out_format = NULL;
	AVStream* avstream = NULL;
	AVCodecContext* avcodec_context = NULL;
	AVCodec* avcodec = NULL;

	AVPacket avpacket;
	int got_picture = 0;
	int result = -1;
	//1. 分配avformat_context内存
	avformat_context = avformat_alloc_context();
	//2. 得到输出格式  Guess format
	out_format = av_guess_format("mjpeg", NULL, NULL);
	avformat_context->oformat = out_format;

	//3. 打开输出文件
	if (avio_open(&avformat_context->pb, output_filename, AVIO_FLAG_READ_WRITE) < 0) {
		printf("avio open failed\n");
		av_free(avframe);
		return -1;
	}

	//4. 给avformat_context添加一个新流avstream
	avstream = avformat_new_stream(avformat_context, 0);
	if (NULL == avstream) {
		printf("new a stream failed\n");
		av_free(avframe);
		return -1;
	}

	//5. 给新avstream的avcodec_context赋值 
	avcodec_context = avstream->codec;
	avcodec_context->codec_type = AVMEDIA_TYPE_VIDEO;
	avcodec_context->codec_id = out_format->video_codec;
	avcodec_context->pix_fmt = AV_PIX_FMT_YUVJ420P;
	avcodec_context->compression_level = 10;
	avcodec_context->time_base.num = 1;
	avcodec_context->time_base.den = 25;

	avcodec_context->width = avframe->width;
	avcodec_context->height = avframe->height;
	av_dump_format(avformat_context, 0, output_filename, 1);

	//6. 新avstream的avcodec_context 寻找编码器
	avcodec = avcodec_find_encoder(avcodec_context->codec_id);
	if (!avcodec) {
		printf("find decoder by codec id failed, codec id: %d\n", avcodec_context->codec_id);
		av_free(avframe);
		return -1;
	}

	//7. 打开编码器
	if (avcodec_open2(avcodec_context, avcodec, NULL) < 0) {
		printf("open encoder failed\n");
		av_free(avframe);
		return -1;
	}

	//8. 给avformat_context写头信息
	avformat_write_header(avformat_context, NULL);

	int y_size = avcodec_context->width * avcodec_context->height;

	//9. 分配一个内存给包  大小为3 * width * height 
	av_new_packet(&avpacket, 3 * y_size);

	//10. 编码
	result = avcodec_encode_video2(avcodec_context, &avpacket, avframe, &got_picture);
	if (result < 0) {
		printf("encode codec failed");
		av_free(avframe);
		return -1;
	}

	if (1 == got_picture) {
		avpacket.stream_index = avstream->index;
		//11. 写帧 
		result = av_write_frame(avformat_context, &avpacket);
	}

	av_free_packet(&avpacket);
	//12. 给avformat_context写尾信息
	av_write_trailer(avformat_context);

	if (NULL != avstream) {
		avcodec_close(avstream->codec);
		av_free(avframe);
	}

	avio_close(avformat_context->pb);
	avformat_free_context(avformat_context);

	return 0;
}

int YUVToBGR24(const char* output_filename, AVFrame* avframe, AVCodecContext* avcodec_context) {
	//初始化scale 像素从YUV转为BGR24
	struct SwsContext* sws_context = NULL;
	sws_context = sws_getContext(avcodec_context->width,
								 avcodec_context->height,
								 avcodec_context->pix_fmt,
								 output_width,
								 output_height,
								 AV_PIX_FMT_BGR24,
								 SWS_BICUBIC,
								 NULL, NULL, NULL);
	if (NULL == sws_context) {
		av_free(avframe);
		printf("get sws context failed\n");
		return -1;
	}

	uint8_t* rgb24_image_buffer = NULL;
	rgb24_image_buffer = (uint8_t*)malloc(output_width * output_height * 3);
	if (NULL == rgb24_image_buffer) {
		printf("malloc memory to rgb image failed");
		av_free(avframe);
		return -1;
	}

	uint8_t* data[1] = { rgb24_image_buffer };
	int linesize[1] = { output_width * 3 };
	//图像格式转换 输入图像的数据 行大小 从开始处理0 目标图像的数据 行大小
	//把avframe像素数据 以height行 每行linesize的字节 送入frame的数据里
	sws_scale(sws_context, avframe->data, avframe->linesize, 0,
			  avframe->height, data, linesize);

	sws_freeContext(sws_context);
	sws_context = NULL;
	
	BGR24ToBMP(output_filename, rgb24_image_buffer);

	av_free(avframe);
	free(rgb24_image_buffer);

	return 0;
}

int BGR24ToBMP(const char* output_filename, uint8_t* bmp_data) {
	BitMapHeader bit_map_header = { 0 };
	BitMapInfoHeader bit_map_info_header = { 0 };

	int bpp = 24;
	
	FILE* fp = NULL;
	fp = fopen(output_filename, "wb");
	if (NULL == fp) {
		printf("open file failed,filename:%s", output_filename);
		return -1;
	}

	//设置bmp结构体的成员
    unsigned short bf_type = 0x4d42;
	bit_map_header.bf_reserved1 = 0;
	bit_map_header.bf_reserved2 = 0;
	bit_map_header.bf_offbits = 0x36;
	bit_map_header.bf_size = 2 + sizeof(BitMapHeader) + sizeof(BitMapInfoHeader) + output_width * output_height * 3;

	bit_map_info_header.bi_size = sizeof(BitMapInfoHeader);
	bit_map_info_header.bi_width = output_width;
	bit_map_info_header.bi_height = 0 - output_height;
	bit_map_info_header.bi_planes = 1;
	bit_map_info_header.bi_bit_count = bpp;
	bit_map_info_header.bi_compression = 0;   //BI_RGB 非压缩
	bit_map_info_header.bi_size_image = 0;
	bit_map_info_header.bi_xpixels_permeter = 5000;
	bit_map_info_header.bi_ypixels_permeter = 5000;
	bit_map_info_header.bi_clr_used = 0;
	bit_map_info_header.bi_clr_important = 0;

    fwrite(&bf_type, sizeof(bf_type), 1, fp);
	fwrite(&bit_map_header, sizeof(BitMapHeader), 1, fp);
	fwrite(&bit_map_info_header, sizeof(BitMapInfoHeader), 1, fp);
	fwrite(bmp_data, output_width * output_height * bpp / 8, 1, fp);

	fclose(fp);
	printf("success generate %d BMP file\n", current_frame_number);

	return 0;
}

int main(int argc, char* argv[]) {
	//1. 注册所有编解码器 协议
	av_register_all();
	avformat_network_init();

	AVFormatContext* avformat_context = NULL;
	AVCodecContext* avcodec_context = NULL;

	//2. 打开媒体文件 内部初始化分配avformat_context内存 
	if (0 != avformat_open_input(&avformat_context, input_filename, NULL, NULL)) {
		printf("open file failed, filename: %s\n", input_filename);
		return -1;
	}

	//3. 找到流信息
	if (avformat_find_stream_info(avformat_context, NULL) < 0) {
		printf("find stream info failed\n");
		return -1;
	}

	video_stream_index = -1;
	//4. 遍历每个音视频流 找到视频流 
	for (int i = 0; i < avformat_context->nb_streams; ++i) {
		if (AVMEDIA_TYPE_VIDEO == avformat_context->streams[i]->codecpar->codec_type) {
			video_stream_index = i;
			break;
		}
	}

	//-1表示没找到
	if (-1 == video_stream_index) {
		printf("can not find a video stream in file, filename: %s\n", input_filename);
		return -1;
	}

	//5. 查找解码器 遍历链表
	AVCodecParameters* avcodec_param = avformat_context->streams[video_stream_index]->codecpar;
	AVCodec* avcodec = avcodec_find_decoder(avcodec_param->codec_id);
	if (NULL == avcodec) {
		printf("find decoder by codec id failed, codec id: %d\n", avcodec_param->codec_id);
		return -1;
	}

	if (NULL != avcodec_context) {
		avcodec_free_context(&avcodec_context);
	}

	//6. 给AVCodecContext分配内存
	avcodec_context = avcodec_alloc_context3(avcodec);
	if (NULL == avcodec_context) {
		printf("alloc codec context failed\n");
		return -1;
	}

	//7. 将avparameter参数(包含了大部分解码器相关信息)赋值给avcodec_context
	if (0 != avcodec_parameters_to_context(avcodec_context, avcodec_param)) {
		printf("avcodec param to avcodec context failed\n");
		return -1;
	}

	//8. 打开解码器
	int result = avcodec_open2(avcodec_context, avcodec, NULL);
	if (0 != result) {
		printf("open decoder failed\n");
		return -1;
	}

	//视频解码
	VideoDecode(avformat_context, avcodec_context);

	if (NULL != avformat_context) {
		avformat_close_input(&avformat_context);
	}

	if (NULL != avcodec_context) {
		avcodec_free_context(&avcodec_context);
	}

	return 0;
}
