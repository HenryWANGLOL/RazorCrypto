# RazorTrade
Razor Trade 是一个超高频交易系统，支持中国股票期货期权的交易

## 开发者
yufeng Wang
xiaoyu Yuan


## 系统架构
Razor Trade 是一个基于RazorPay的股票期货期权交易系统，采用微服务架构，包括以下组件：


## 编译
1. 修改根目录下CMAKELists.txt中的各种路径，匹配自己的系统
2. mkdir b2
3. cd b2 && cmake .. && make

## Demo



### CTP行情启动
/opt/RazorTrade/release/CTP_MD /home/op/RazorTrade/config/ctp_md_config.yaml

### CTP柜台启动
/opt/RazorTrade/release/CTP_TD /home/op/RazorTrade/config/ctp_si_config.yaml

/opt/RazorTrade/release/CTP_TD /home/op/RazorTrade/config/ctp_td_config.yaml

/opt/RazorTrade/release/stgrunner /home/op/RazorTrade/config/stg_config.yml
### python行情读取
python3 /root/RazorTrade/Tools/pymdreader/demo/pymdreader.py