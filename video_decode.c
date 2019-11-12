#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "libavutil/avutil.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#define true 1

//bmp�ṹ�� bmp�洢��ʽ��С�˴洢  �����ֽڷ��ڵ͵�ַ ���ֽڷ��ڸߵ�ַ
typedef struct {
	unsigned int bf_size;           //4 λͼ�ļ��Ĵ�С
	unsigned short bf_reserved1;    //2 λͼ�ļ������֣�����Ϊ0
	unsigned short bf_reserved2;    //2 λͼ�ļ������֣�����Ϊ0
	unsigned int bf_offbits;        //4 λͼ���ݵ���ʼλ�ã��������λͼ�ļ�ͷ��ƫ������ʾ�����ֽ�Ϊ��λ
} BitMapHeader;

typedef struct {
	unsigned int bi_size;		   //4 �ṹ����ռ���ֽ���
	int bi_width;				   //4 λͼ�Ŀ��
	int bi_height;				   //4 λͼ�ĸ߶�
	unsigned short bi_planes;	   //2 λͼ��ƽ����������Ϊ1
	unsigned short bi_bit_count;   //2 ÿ�����������λ����������1(˫ɫ), 4(16ɫ)��8(256ɫ)��24(���ɫ)֮һ
	unsigned int bi_compression;   //4 λͼѹ�����ͣ������� 0(��ѹ��),1(BI_RLE8ѹ������)��2(BI_RLE4ѹ������)֮һ
	unsigned int bi_size_image;    //4 ʵ��λͼ����ռ�õ��ֽ���
	int bi_xpixels_permeter;	   //4 λͼxˮƽ�ֱ��ʣ�ÿ��������
	int bi_ypixels_permeter;	   //4 λͼy��ֱ�ֱ��ʣ�ÿ��������
	unsigned int bi_clr_used;	   //4 λͼʵ��ʹ�õ���ɫ��
	unsigned int bi_clr_important; //4 λͼ��ʾ��������Ҫ����ɫ��
} BitMapInfoHeader;

const char* input_filename = "/home/yipeng/videos/2.mp4";//�ļ���
int video_stream_index = -1;							//��Ƶ������
size_t current_frame_number = 0;						//��ǰ�õ���֡����
size_t frame_number = 0;								//��֡��
size_t pump_frame_rate = 3;								//��֡��
size_t output_width = 640;								//���rgb�Ŀ�
size_t output_height = 480;								//���rgb�ĸ�

static int VideoDecode(AVFormatContext* avformat_context, AVCodecContext* avcodec_context);
static int EncodeYUVToJPG(const char* output_filename, AVFrame* avframe);
static int YUVToBGR24(const char* output_filename, AVFrame* avframe, AVCodecContext* avcodec_context);
static int BGR24ToBMP(const char* output_filename, uint8_t* bmp_data);

int VideoDecode(AVFormatContext* avformat_context, AVCodecContext* avcodec_context) {
	//������ļ���
	char image_name[20];
	int send_result = -1;
	int receive_result = -1;

	while (true) {
		AVPacket avpacket;

		while (true) {
			av_init_packet(&avpacket);
			//5. ��ȡ�����е�һ֡��Ƶ ���һ֡��Ƶ��ѹ������AVPacket
			//H264��һ��AVPacket��Ӧһ��NAL 
			if (av_read_frame(avformat_context, &avpacket) < 0) {
				printf("read frame fail\n");
				av_packet_unref(&avpacket);
				return -2;
			}

			//��������İ������� �� ��Ƶ������ �����
			if (video_stream_index != avpacket.stream_index) {
				//�ͷŰ�
				av_packet_unref(&avpacket);
				continue;
			}
			else {
				break;
			}
		}

		//������ǹؼ�֡ �˳�
		/*if (avpacket.flags != AV_PKT_FLAG_KEY) {
		    av_packet_unref(&avpacket);
		    continue;
		}*/

		//10 ���Ͱ�
		send_result = avcodec_send_packet(avcodec_context, &avpacket);
		if (0 != send_result
			&& AVERROR(EAGAIN) != send_result) {
			printf("send codec packet failed, send result: %d\n", send_result);
			av_packet_unref(&avpacket);
			return -1;
		}

		//11. Ϊ֡�����ڴ� 
		AVFrame* avframe = av_frame_alloc();
		if (NULL == avframe) {
			printf("alloc frame failed\n");
			av_packet_unref(&avpacket);
			return -1;
		}

		//12. ����֡
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
	//1. ����avformat_context�ڴ�
	avformat_context = avformat_alloc_context();
	//2. �õ������ʽ  Guess format
	out_format = av_guess_format("mjpeg", NULL, NULL);
	avformat_context->oformat = out_format;

	//3. ������ļ�
	if (avio_open(&avformat_context->pb, output_filename, AVIO_FLAG_READ_WRITE) < 0) {
		printf("avio open failed\n");
		av_free(avframe);
		return -1;
	}

	//4. ��avformat_context���һ������avstream
	avstream = avformat_new_stream(avformat_context, 0);
	if (NULL == avstream) {
		printf("new a stream failed\n");
		av_free(avframe);
		return -1;
	}

	//5. ����avstream��avcodec_context��ֵ 
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

	//6. ��avstream��avcodec_context Ѱ�ұ�����
	avcodec = avcodec_find_encoder(avcodec_context->codec_id);
	if (!avcodec) {
		printf("find decoder by codec id failed, codec id: %d\n", avcodec_context->codec_id);
		av_free(avframe);
		return -1;
	}

	//7. �򿪱�����
	if (avcodec_open2(avcodec_context, avcodec, NULL) < 0) {
		printf("open encoder failed\n");
		av_free(avframe);
		return -1;
	}

	//8. ��avformat_contextдͷ��Ϣ
	avformat_write_header(avformat_context, NULL);

	int y_size = avcodec_context->width * avcodec_context->height;

	//9. ����һ���ڴ����  ��СΪ3 * width * height 
	av_new_packet(&avpacket, 3 * y_size);

	//10. ����
	result = avcodec_encode_video2(avcodec_context, &avpacket, avframe, &got_picture);
	if (result < 0) {
		printf("encode codec failed");
		av_free(avframe);
		return -1;
	}

	if (1 == got_picture) {
		avpacket.stream_index = avstream->index;
		//11. д֡ 
		result = av_write_frame(avformat_context, &avpacket);
	}

	av_free_packet(&avpacket);
	//12. ��avformat_contextдβ��Ϣ
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
	//��ʼ��scale ���ش�YUVתΪBGR24
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
	//ͼ���ʽת�� ����ͼ������� �д�С �ӿ�ʼ����0 Ŀ��ͼ������� �д�С
	//��avframe�������� ��height�� ÿ��linesize���ֽ� ����frame��������
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

	//����bmp�ṹ��ĳ�Ա
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
	bit_map_info_header.bi_compression = 0;   //BI_RGB ��ѹ��
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
	//1. ע�����б������ Э��
	av_register_all();
	avformat_network_init();

	AVFormatContext* avformat_context = NULL;
	AVCodecContext* avcodec_context = NULL;

	//2. ��ý���ļ� �ڲ���ʼ������avformat_context�ڴ� 
	if (0 != avformat_open_input(&avformat_context, input_filename, NULL, NULL)) {
		printf("open file failed, filename: %s\n", input_filename);
		return -1;
	}

	//3. �ҵ�����Ϣ
	if (avformat_find_stream_info(avformat_context, NULL) < 0) {
		printf("find stream info failed\n");
		return -1;
	}

	video_stream_index = -1;
	//4. ����ÿ������Ƶ�� �ҵ���Ƶ�� 
	for (int i = 0; i < avformat_context->nb_streams; ++i) {
		if (AVMEDIA_TYPE_VIDEO == avformat_context->streams[i]->codecpar->codec_type) {
			video_stream_index = i;
			break;
		}
	}

	//-1��ʾû�ҵ�
	if (-1 == video_stream_index) {
		printf("can not find a video stream in file, filename: %s\n", input_filename);
		return -1;
	}

	//5. ���ҽ����� ��������
	AVCodecParameters* avcodec_param = avformat_context->streams[video_stream_index]->codecpar;
	AVCodec* avcodec = avcodec_find_decoder(avcodec_param->codec_id);
	if (NULL == avcodec) {
		printf("find decoder by codec id failed, codec id: %d\n", avcodec_param->codec_id);
		return -1;
	}

	if (NULL != avcodec_context) {
		avcodec_free_context(&avcodec_context);
	}

	//6. ��AVCodecContext�����ڴ�
	avcodec_context = avcodec_alloc_context3(avcodec);
	if (NULL == avcodec_context) {
		printf("alloc codec context failed\n");
		return -1;
	}

	//7. ��avparameter����(�����˴󲿷ֽ����������Ϣ)��ֵ��avcodec_context
	if (0 != avcodec_parameters_to_context(avcodec_context, avcodec_param)) {
		printf("avcodec param to avcodec context failed\n");
		return -1;
	}

	//8. �򿪽�����
	int result = avcodec_open2(avcodec_context, avcodec, NULL);
	if (0 != result) {
		printf("open decoder failed\n");
		return -1;
	}

	//��Ƶ����
	VideoDecode(avformat_context, avcodec_context);

	if (NULL != avformat_context) {
		avformat_close_input(&avformat_context);
	}

	if (NULL != avcodec_context) {
		avcodec_free_context(&avcodec_context);
	}

	return 0;
}
