/* sparkline.c -- ASCII Sparklines
 * This code is modified from http://github.com/antirez/aspark and adapted
 * in order to return SDS strings instead of outputting directly to
 * the terminal.
 *
 * ---------------------------------------------------------------------------
 *
 * Copyright(C) 2011-2014 Salvatore Sanfilippo <antirez@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "server.h"

#include <math.h>

/* This is the charset used to display the graphs, but multiple rows are used
 * to increase the resolution. */
static char charset[] = "_-`";
static char charset_fill[] = "_o#";
static int charset_len = sizeof(charset)-1;//2
static int label_margin_top = 1;

/* ----------------------------------------------------------------------------
 * Sequences are arrays of samples we use to represent data to turn
 * into sparklines. This is the API in order to generate a sparkline:
 *
 * struct sequence *seq = createSparklineSequence();
 * sparklineSequenceAddSample(seq, 10, NULL);
 * sparklineSequenceAddSample(seq, 20, NULL);
 * sparklineSequenceAddSample(seq, 30, "last sample label");
 * sds output = sparklineRender(sdsempty(), seq, 80, 4, SPARKLINE_FILL);
 * freeSparklineSequence(seq);
 * ------------------------------------------------------------------------- */

/* Create a new sequence. */
struct sequence *createSparklineSequence(void) {
    struct sequence *seq = zmalloc(sizeof(*seq));
    seq->length = 0;
    seq->samples = NULL;
    return seq;
}

/* Add a new sample into a sequence. */
//增加信息节点
void sparklineSequenceAddSample(struct sequence *seq, double value, char *label) {
    //复制一份label，之后对label的修改对原始数据不会生效，zmalloc申请的空间sds
    label = (label == NULL || label[0] == '\0') ? NULL : zstrdup(label);
    //修改最大最小值
    if (seq->length == 0) {
        seq->min = seq->max = value;
    } else {
        if (value < seq->min) seq->min = value;
        else if (value > seq->max) seq->max = value;
    }
    //多申请一个空间
    seq->samples = zrealloc(seq->samples,sizeof(struct sample)*(seq->length+1));
    //最后一个节点的数据就是新加的数据，下标为seq->length
    seq->samples[seq->length].value = value;
    seq->samples[seq->length].label = label;
    seq->length++;
    if (label) seq->labels++;
}

/* Free a sequence. */
//清空sparkline
void freeSparklineSequence(struct sequence *seq) {
    int j;
    //这边的详细流程是这样的
    /*
    从低到高释放
    先释放label sparklineSequenceAddSample->zstrdup(label)
    在释放samples sparklineSequenceAddSample->zrealloc(seq->samples,sizeof(struct sample)*(seq->length+1))
    最后释放seq createSparklineSequence-> zmalloc(sizeof(*seq))
    */
    for (j = 0; j < seq->length; j++)
        //释放之前add申请的zmalloc的内存
        zfree(seq->samples[j].label);
    zfree(seq->samples);
    zfree(seq);
}

/* ----------------------------------------------------------------------------
 * ASCII rendering of sequence
 * ------------------------------------------------------------------------- */

/* Render part of a sequence, so that render_sequence() call call this function
 * with differnent parts in order to create the full output without overflowing
 * the current terminal columns. */
/* 渲染出这个图线信息 */
/*
sds output, //结果字符串
struct sequence *seq, //原始分析数据
int rows, //
int offset,  //seq.samples的下标
int len, //长度
int flags
*/
//将seq中的seq->samples[offset,offset+len)的样点转换成rows行字符串的形式，存储在output中
sds sparklineRenderRange(sds output, struct sequence *seq, int rows, int offset, int len, int flags) {
    int j;
    //获取最大最小之间的间距
    double relmax = seq->max - seq->min;
    int steps = charset_len*rows;
    int row = 0;
    //获取len长度的sds
    char *chars = zmalloc(len);
    int loop = 1;
    //查看标志位0，1，2
    int opt_fill = flags & SPARKLINE_FILL;
    //是否需要计算对数
    int opt_log = flags & SPARKLINE_LOG_SCALE;

    if (opt_log) {
        //C 库函数 double log(double x) 返回 x 的自然对数（基数为 e 的对数）。
        relmax = log(relmax+1);
    } else if (relmax == 0) {
        relmax = 1;
    }

    while(loop) {
        loop = 0;
        //将chars全用空格填满
        memset(chars,' ',len);
        //遍历seq.samples
        for (j = 0; j < len; j++) {
            struct sample *s = &seq->samples[j+offset];
            //获取当前节点和最小值的距离
            double relval = s->value - seq->min;
            int step;
            //如果需要计算对数
            if (opt_log) relval = log(relval+1);
            //这个数据类型不对怎么过的
            step = (int) (relval*steps)/relmax;
            if (step < 0) step = 0;
            if (step >= steps) step = steps-1;
            //如果没有到需要的rows
            if (row < rows) {
                /* Print the character needed to create the sparkline */
                /* step控制输出的字符是哪一个 */
                int charidx = step-((rows-row-1)*charset_len);
                loop = 1;
                if (charidx >= 0 && charidx < charset_len) {
                    chars[j] = opt_fill ? charset_fill[charidx] :
                                          charset[charidx];
                } else if(opt_fill && charidx >= charset_len) {
                    chars[j] = '|';
                }
            } else {
                 //对于大于row的行，不处理样点，只处理label
                /* Labels spacing */
                if (seq->labels && row-rows < label_margin_top) {
                    loop = 1;
                    break;
                }
                /* Print the label if needed. */
                if (s->label) {
                    int label_len = strlen(s->label);
                    int label_char = row - rows - label_margin_top;

                    if (label_len > label_char) {
                        loop = 1;
                        chars[j] = s->label[label_char];
                    }
                }
            }
        }
        if (loop) {
            row++;
            output = sdscatlen(output,chars,len);
            output = sdscatlen(output,"\n",1);
        }
    }
    //释放chars
    zfree(chars);
    return output;
}

/* Turn a sequence into its ASCII representation */
/*
最终所有的样点是通过调用函数将每个样点转换为ASCII码形式，来形成最终的折线图的。
　　通过参数columns和rows限定了最后输出的图形的行数和列数，通过参数flags来选择每个信息点的表示方式。
　　当seq中样点数不大于限定的列数时，只需执行一次sparklineRenderRange（）就可以将序列中每个样点都转换为字符串的形式。
　　最后将输出的折线图按照字符串的形式存储在output中。
*/
sds sparklineRender(sds output, struct sequence *seq, int columns, int rows, int flags) {
    int j;

    for (j = 0; j < seq->length; j += columns) {
        //剩余长度
        int sublen = (seq->length-j) < columns ? (seq->length-j) : columns;
        //第一行不加换行符
        if (j != 0) output = sdscatlen(output,"\n",1);
        output = sparklineRenderRange(output, seq, rows, j, sublen, flags);
    }
    return output;
}

