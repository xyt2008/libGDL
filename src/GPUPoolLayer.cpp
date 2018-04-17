/**
 * file :	GPUPoolLayer.cpp
 * author :	Rex
 * create :	2017-12-05 14:37
 * func : 
 * history:
 */

#include "GPUPoolLayer.h"
#include "GPUConvLayer.h"
#include "ConvFrameBuffer.h"

static const char* g_pool_vertext_shader = SHADER30_STRING(
    in vec4 position;
    in vec4 inputTextureCoordinate;
    out vec2 textureCoordinate;
    in vec4 inputChannelMap;
    out vec2 channelMap;
    void main()
    {
        gl_Position = position;
        textureCoordinate = inputTextureCoordinate.xy;
        channelMap = inputChannelMap.xy;
    }
);
static const char* g_pool_max_fragment = SHADER30_STRING(
    in highp vec2 textureCoordinate;
    in highp vec2 channelMap;
    out highp uvec4 out_color;
    const int pool_size = %d;
    // 0为输入纹理，1为输出对应的输入纹理位置
    uniform highp sampler2D inputImageTexture[1];

    uniform highp float w_step;
    uniform highp float h_step;
    uniform highp float w_channel_step;
    uniform highp float h_channel_step;
#define encodeFloats(v) uvec4(floatBitsToUint((v).r), floatBitsToUint((v).g), floatBitsToUint((v).b), floatBitsToUint((v).a))
#define decodeFloats(v) vec4(uintBitsToFloat((v).r), uintBitsToFloat((v).g), uintBitsToFloat((v).b), uintBitsToFloat((v).a))

    void main(){
        highp vec4 maximum = vec4(0.0, 0.0, 0.0, 0.0);
        // highp vec4 pos = texture(inputImageTexture[1], textureCoordinate.xy);
        // 当尺寸为奇数时候，最后一行或者一列和自己做比较，此时poo尺寸为1x2或者2x1
        highp float w_border = channelMap.x + w_channel_step;
        highp float h_border = channelMap.y + h_channel_step;
        for(int h=0; h<pool_size && textureCoordinate.y+float(h)*h_step<h_border; h++){
            for(int w=0; w<pool_size && textureCoordinate.x+float(w)*w_step<w_border; w++){
                highp vec4 color = texture(inputImageTexture[0], textureCoordinate + vec2(float(w)*w_step, float(h)*h_step));
                maximum = vec4(max(maximum.r, color.r), max(maximum.g, color.g), max(maximum.b, color.b), max(maximum.a, color.a));
            }
        }

        out_color = encodeFloats(maximum);
        // out_color = texture(inputImageTexture[0], pos.rg);
    }
);

GPUPoolLayer::GPUPoolLayer(int channel_count, int pool_size, conv_pool_t pool_type, int next_padding, const char* name):
GPULayerBase(channel_count, name),
m_pool_size(pool_size),
m_next_padding(next_padding),
m_pool_type(pool_type){
    m_inputs = 1;
    m_map_buffer = NULL;
    m_channel_buffer = NULL;
    char shader[10240];
    snprintf(shader, sizeof(shader), g_pool_max_fragment, pool_size);
    m_program = new GPUProgram(g_pool_vertext_shader, shader, m_filter_name.c_str());
    m_channel_map = m_program->attributeIndex("inputChannelMap");
    glEnableVertexAttribArray(m_channel_map);
    init();
}

GPUPoolLayer::~GPUPoolLayer(){
    if (m_map_buffer!=NULL) {
        delete m_map_buffer;
    }
    if (m_channel_buffer!=NULL) {
        delete m_channel_buffer;
    }
}

void GPUPoolLayer::render(){
#if DEBUG_FILTER_NAME
    err_log("filter name: %s texture: %d", m_filter_name.c_str(), m_input_buffers[0]->m_texture);
#endif
    
    GPUCheckGlError(m_filter_name.c_str(), true, false);
    GPUContext* context = GPUContext::shareInstance();
    context->glContextLock();   // 加锁，防止此时设置参数
    
    context->setActiveProgram(m_program);
    activeOutFrameBuffer();
    for (int i=0; i<m_inputs; i++) {
        m_input_buffers[i]->activeTexture(GL_TEXTURE0+i);
        glUniform1i(m_input_textures[i], 0+i);
    }
    
    m_coordinate_buffer->activeBuffer(m_input_coordinate);
    m_vertex_buffer->activeBuffer(m_position);
    m_channel_buffer->activeBuffer(m_channel_map);
    glDrawElements(GL_TRIANGLES, m_outx_count*m_outy_count*6, GL_UNSIGNED_SHORT, ((ConvFrameBuffer*)m_outbuffer)->vertexIndexs());
    glFlush();
    
    m_coordinate_buffer->disableBuffer(m_input_coordinate);
    m_vertex_buffer->disableBuffer(m_position);
    m_channel_buffer->disableBuffer(m_channel_map);
    m_outbuffer->unactive();
    
    GPUCheckGlError(m_filter_name.c_str(), true, false);
    context->glContextUnlock();
}

void GPUPoolLayer::activeOutFrameBuffer(){
    ConvFrameBuffer* buffer = dynamic_cast<ConvFrameBuffer*>(m_last_layer->m_outbuffer);
    int channle_width = int(buffer->m_channel_width*1.0/m_pool_size+0.5);
    int channel_height = int(buffer->m_channel_height*1.0/m_pool_size+0.5);
    
    if (m_outbuffer == NULL) {
        m_outx_count = buffer->m_x_count;
        m_outy_count = buffer->m_y_count;
        m_outbuffer = ConvBufferCache::getFrameBuffer(channle_width, channel_height, m_next_padding, buffer->m_x_count, buffer->m_y_count, buffer->m_channel_count);
        setOutputSize(m_outbuffer->m_width, m_outbuffer->m_height);
        // 坐标纹理
        //m_map_buffer = mapTexture(channle_width, channel_height);
        //m_input_buffers[1] = m_map_buffer;
        memset(m_clear_color, 0, sizeof(float)*4);
        
        float* vertex = ((ConvFrameBuffer*)m_outbuffer)->convVertices();
        int vertexCount = ((ConvFrameBuffer*)m_outbuffer)->vertexCount();
        m_vertex_buffer = new GPUVertexBuffer(vertexCount);
        m_vertex_buffer->setBuffer(vertex);
        m_coordinate_buffer = new GPUVertexBuffer(vertexCount);
        m_channel_buffer = new GPUVertexBuffer(vertexCount);
        coordinateMap();
        /*
        m_coordinate_buffer = new GPUVertexBuffer(m_outx_count*m_outy_count*4);
        m_coordinate_buffer->setBuffer(((ConvFrameBuffer*)m_outbuffer)->poolCoordinates());
        
        m_vertex_buffer = new GPUVertexBuffer(m_outx_count*m_outy_count*4);
        m_vertex_buffer->setBuffer(((ConvFrameBuffer*)m_outbuffer)->poolVertices());
         */
    }
    else{
        m_outbuffer = ConvBufferCache::getFrameBuffer(channle_width, channel_height, m_next_padding, buffer->m_x_count, buffer->m_y_count, buffer->m_channel_count);
    }
    m_outbuffer->activeBuffer();
    
    glClearColor(m_clear_color[0], m_clear_color[1], m_clear_color[2], m_clear_color[3]);
    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
}

void GPUPoolLayer::setFrameSize(uint32_t width, uint32_t height){
    GPUFilter::setFrameSize(width, height);
    ConvFrameBuffer* buffer = dynamic_cast<ConvFrameBuffer*>(m_last_layer->m_outbuffer);
    // 步长为输入纹理
    setFloat("w_step", 1.0/buffer->m_width);
    setFloat("h_step", 1.0/buffer->m_height);
    setFloat("w_channel_step", buffer->m_channel_width*1.0/buffer->m_width);
    setFloat("h_channel_step", buffer->m_channel_height*1.0/buffer->m_height);
}

void GPUPoolLayer::coordinateMap(){
    ConvFrameBuffer* outbuffer = dynamic_cast<ConvFrameBuffer*>(m_outbuffer);
    ConvFrameBuffer* inbuffer = dynamic_cast<ConvFrameBuffer*>(m_input_buffers[0]);
    
    int channel_rect = ConvFrameBuffer::convChannelRect();
    vector<float>   coor_buffer;
    vector<float>   channel_buffer;
    
    float w_channel_step = 1.0*inbuffer->m_channel_width/inbuffer->m_width;
    float w_padding_step = 1.0*inbuffer->m_padding_size/inbuffer->m_width;
    float h_channel_step = 1.0*inbuffer->m_channel_height/inbuffer->m_height;
    float h_padding_step = 1.0*inbuffer->m_padding_size/inbuffer->m_height;
    float w_step = 1.0/inbuffer->m_width;
    float h_step  = 1.0/inbuffer->m_height;
    
    for (int h=0; h<outbuffer->m_y_count; h++) {
        for (int w=0; w<outbuffer->m_x_count && h*outbuffer->m_x_count+w<outbuffer->m_channel_count; w++) {
            // 0.1,0.3,0.5,0.7,0.9->0.1,0.5,0.9,顶点为-0.1,1.1。偶数顶点:0.123,0.375,0.625,0.875->0.125,0.625，顶点为-0.125,0.875
            float start_x = (w_channel_step + 2.0*w_padding_step)*w + w_step/2.0 - w_step;
            float end_x = start_x + ((inbuffer->m_channel_width+1)/2)*w_step*2;
            float start_y = (h_channel_step + 2.0*h_padding_step)*h + h_step/2.0 - h_step;
            float end_y = start_y + ((inbuffer->m_channel_height+1)/2)*h_step*2;
            for (int hi=0; hi<channel_rect; hi++) {
                for (int wi=0; wi<channel_rect; wi++) {
                    float x = start_x + (end_x-start_x)*wi/(channel_rect-1.0);
                    float y = start_y + (end_y-start_y)*hi/(channel_rect-1.0);
                    coor_buffer.push_back(x);
                    coor_buffer.push_back(y);
                    float channel_x = (w_channel_step + 2.0*w_padding_step)*w + w_step/2.0;
                    float channel_y = (h_channel_step + 2.0*h_padding_step)*h + h_step/2.0;
                    channel_buffer.push_back(channel_x);
                    channel_buffer.push_back(channel_y);
                }
            }
        }
    }
    m_coordinate_buffer->setBuffer(&coor_buffer[0]);
    m_channel_buffer->setBuffer(&channel_buffer[0]);
}

GPUFrameBuffer* GPUPoolLayer::mapTexture(int channel_width, int channel_height){
    ConvFrameBuffer* buffer = dynamic_cast<ConvFrameBuffer*>(m_last_layer->m_outbuffer);
    float* values = (float*)calloc(1, m_outbuffer->m_width*m_outbuffer->m_height*4*sizeof(float));
    float w_step = 1.0/buffer->m_width;
    float h_step = 1.0/buffer->m_height;
    
    for (int h=m_next_padding; h<m_out_height-m_next_padding; h++) {
        int ch = h/(channel_height + 2*m_next_padding);
        // 需要的是在channel中的偏移，减掉一个padding
        int hbias = h%(channel_height + 2*m_next_padding) - m_next_padding;
        // 在输入纹理中的y起始坐标
        float y = (ch*buffer->m_channel_height + hbias*m_pool_size)*h_step + h_step/2.0;
        for (int w = m_next_padding; w<m_out_width-m_next_padding; w++) {
            int cw = w/(channel_width + 2*m_next_padding);
            // 需要的是在channel中的偏移，减掉一个padding
            int wbias = w%(channel_width + 2*m_next_padding) - m_next_padding;
            float x = (cw*buffer->m_channel_width + wbias*m_pool_size)*w_step + w_step/2.0;
            values[(h*m_out_width+w)*4] = x;
            values[(h*m_out_width+w)*4+1] = y;
            // 后两个float用来保存当前在第几个通道上
            values[(h*m_out_width+w)*4+2] = cw;
            values[(h*m_out_width+w)*4+3] = ch;
        }
    }
//    printf("#### pool map #####\n");
//    for (int h=0; h<m_out_height; h++) {
//        for (int w=0; w<m_out_width; w++) {
//            printf("%0.3f ", values[(h*m_out_width+w)*2]);
//        }
//        printf("\n");
//    }
    gpu_frame_option_t option;
    memcpy(&option, GPUFrameBuffer::nearestFrameOption(), sizeof(gpu_frame_option_t));
    option.color_format = GL_RGBA32F;
    option.format = GL_RGBA;
    option.type = GL_FLOAT;
    
    GPUFrameBuffer* map_buffer = new GPUFrameBuffer(m_outbuffer->m_width, m_outbuffer->m_height, &option, true);
    map_buffer->setPixels(values);
    free(values);
    return map_buffer;
}

void GPUPoolLayer::diff(){
    if (m_values == NULL) {
        return;
    }
    
    float* rgba = (float*)malloc(m_outbuffer->m_width*m_outbuffer->m_height*4*4);
    m_outbuffer->getPixels((uint8_t*)rgba);
    ConvFrameBuffer* buffer = dynamic_cast<ConvFrameBuffer*>(m_outbuffer);
    for (int hc=0; hc<buffer->m_y_count; hc++) {
        for (int wc=0; wc<buffer->m_x_count; wc++) {
            int x_start = wc*(buffer->m_channel_width + 2*buffer->m_padding_size) + buffer->m_padding_size;
            int y_start = hc*(buffer->m_channel_height + 2*buffer->m_padding_size) + buffer->m_padding_size;
            for (int h=0; h<buffer->m_channel_height; h++) {
                for (int w=0; w<buffer->m_channel_width; w++) {
                    float r = rgba[(h+y_start)*m_outbuffer->m_width*4+(w+x_start)*4];
                    float g = rgba[(h+y_start)*m_outbuffer->m_width*4+(w+x_start)*4+1];
                    float b = rgba[(h+y_start)*m_outbuffer->m_width*4+(w+x_start)*4+2];
                    float a = rgba[(h+y_start)*m_outbuffer->m_width*4+(w+x_start)*4+3];
                    
                    float f1 = m_values[(hc*buffer->m_x_count + wc)*(buffer->m_channel_width*buffer->m_channel_height)*4 + h*buffer->m_channel_width+w];
                    float f2 = m_values[(hc*buffer->m_x_count + wc)*(buffer->m_channel_width*buffer->m_channel_height)*4 + (buffer->m_channel_width*buffer->m_channel_height) + h*buffer->m_channel_width+w];
                    float f3 = m_values[(hc*buffer->m_x_count + wc)*(buffer->m_channel_width*buffer->m_channel_height)*4 + (buffer->m_channel_width*buffer->m_channel_height)*2 + h*buffer->m_channel_width+w];
                    float f4 = m_values[(hc*buffer->m_x_count + wc)*(buffer->m_channel_width*buffer->m_channel_height)*4 + (buffer->m_channel_width*buffer->m_channel_height)*3 + h*buffer->m_channel_width+w];
                    if (abs(r-f1)>1) {
                        printf("diff pos[r:%d/%d/%d] val[%f, %f]\n", (hc*buffer->m_x_count+wc)*4, w,h, r, f1);
                    }
                    if (abs(g-f2)>1) {
                        printf("diff pos[g:%d/%d/%d] val[%f, %f]\n", (hc*buffer->m_x_count+wc)*4+1, w,h, g, f2);
                    }
                    if (abs(b-f3)>1) {
                        printf("diff pos[b:%d/%d/%d] val[%f, %f]\n", (hc*buffer->m_x_count+wc)*4+2, w,h, b, f3);
                    }
                    if (abs(a-f4)>1) {
                        printf("diff pos[a:%d/%d/%d] val[%f, %f]\n", (hc*buffer->m_x_count+wc)*4+3, w,h, a, f4);
                    }
                }
            }
        }
    }
    
    free(rgba);
}
