aof文件存储格式
*2\r\n$6\r\nSELECT\r\n$1\r\n1\r\n
格式解析
命令 select 1
*2--两个参数
\r\n分隔符
$6--select长度
SELECT--cmd指令
$1--1长度
1--数据

# 通用格式
数据个数\r\n数据1长度\r\n数据1\r\n数据2长度\r\n数据2\r\n