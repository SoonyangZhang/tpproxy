# tpproxy
```
/*  
 * 
 *                       _oo0oo_ 
 *                      o8888888o 
 *                      88" . "88 
 *                      (| -_- |) 
 *                      0\  =  /0 
 *                    ___/`---'\___ 
 *                  .' \\|     |// '. 
 *                 / \\|||  :  |||// \ 
 *                / _||||| -卍-|||||- \ 
 *               |   | \\\  -  /// |   | 
 *               | \_|  ''\---/''  |_/ | 
 *               \  .-\__  '-'  ___/-. / 
 *             ___'. .'  /--.--\  `. .'___ 
 *          ."" '<  `.___\_<|>_/___.' >' "". 
 *         | | :  `- \`.;`\ _ /`;.`/ - ` : | | 
 *         \  \ `_.   \_ __\ /__ _/   .-` /  / 
 *     =====`-.____`.___ \_____/___.-`___.-'===== 
 *                       `=---=' 
 *                        
 *
 *  
 */  
```
tcp transparent proxy
From CmakeLists.txt, you could find it depends on   
the code in https://github.com/SoonyangZhang/net  
```  
include_directories(${CMAKE_SOURCE_DIR}/logging)  
include_directories(${CMAKE_SOURCE_DIR}/base)  
include_directories(${CMAKE_SOURCE_DIR}/tcp)     
```  
Download the project and put the folders (logging,base,tcp) under tpproxy.  
# Build
```
cd build  
cmake ..
make
```
Test  
```
cd build   
sudo su  
python 3h1s.py  
xterm h1 h2 h3
```
In h3 shell  
```
iperf3 -s  
```
In h2 shell  
```
./t_proxy -p 2223  
``` 
In h1 shell
```
iperf3  -c 10.0.2.2 -i 1 -t 20  
```




