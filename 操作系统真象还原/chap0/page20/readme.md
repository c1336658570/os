此程序用来解释大小端
小端：因为低位在低字节，强制转换数据型时不需要再调整字节了。
大端：有符号数，其字节最高位不仅表示数值本身，还起到了符号的作用。符号位固定为第一字
节，也就是最高位占据最低地址，符号直接可以取出来，容易判断正负。
使用file editionCheck.bin或readelf -e ./editionCheck.bin或readelf -h ./editionCheck.bin查看大小端