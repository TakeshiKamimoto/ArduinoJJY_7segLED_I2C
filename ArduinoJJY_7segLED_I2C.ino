// JJY time code decoder clock using I2C 7segment LED indicator.
//  2022.1.25
#include <Arduino.h>
#include <TimerOne.h>
#include <Wire.h>

#define I2C_SLAVE_ADDRESS 0x10
#define PIN 2


//パルス同期、割り込みタイマ関係
volatile uint32_t SampleClock;
volatile int8_t timeCounter;
volatile  bool  timerIntFlag;
uint32_t  old_time = 0;
uint32_t  old_calltime = 0;
uint8_t   syncCheckCount = 0;
volatile uint8_t  JJYpulseFeed;

//デコード、時計関係
uint8_t d_year, d_week, d_month, d_day, d_hour, d_min;
const byte month_day[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
uint8_t hh, mm, MM, DD, YY;
uint8_t ss;
bool  markerCheckOk, MparityCheckOk, HparityCheckOk;
bool  firstLoop = true;
char  buff[10];
uint8_t markerOkCount, pulseCodeErrorCount;
bool  minCodeHealthy, hourCodeHealthy, dateCodeHealthy, yearCodeHealthy;


//I2Cデータ送信処理ルーチン ******************************************
void I2CwriteByte(char deviceAddress, char bufferAddress, char data) {
  Wire.beginTransmission(deviceAddress);
  Wire.write(bufferAddress);
  Wire.write(data);
  Wire.endTransmission();
}

//**** JJYパルス同期のための割り込み処理初期設定　**********************************
void synchronizer_setup() {
  //JJYパルスをGPIO割り込みで検出するための設定
  attachInterrupt(digitalPinToInterrupt(PIN), interrupt_callback, RISING);

  //タイマー割り込み設定
  Timer1.initialize(1000000);
  Timer1.attachInterrupt(synchronized_callback);
  Timer1.stop();
}


//**** GPIO割り込み処理ルーチン ******************************************
void interrupt_callback() {//DI割り込みで呼ばれるルーチン。JJYパルスと同期してTimer1を起動させる。
  uint32_t now     = micros();
  int32_t interval = now - old_time;

  JJYpulseFeed = 3;

  if ( interval >= 950000 && interval <= 1050000 ) {
    syncCheckCount++;
    
    if ( syncCheckCount > 4 ) {
      Timer1.start();
      syncCheckCount = 0;
      //Serial.println("!Synchronized.");  
    }else {
      //Serial.print("Synchronize check count = ");
      //Serial.println(syncCheckCount);
    }
  }

  old_time = now;
}

//**** タイマ割り込み処理ルーチン ******************************************
void synchronized_callback() {
  uint32_t now = micros();
  int32_t interval = now - old_calltime;

  if( JJYpulseFeed > 0){
      JJYpulseFeed--;  //JJYパルス入力が止まるとここがゼロになる
  }

  if (interval < 500000)
    return;

  old_calltime = now;

  timerIntFlag = true;

  internalClockIncrement();
}




//パルス信号をスキャン+解析してパルスコードとして返すルーチン ******************************************
int8_t get_code(void) {
    
  int8_t scanbit;
  int8_t ret_code;
  bool scan[4];
  char buf[60];

    
    
    //ここからパルス信号スキャン開始
      while (!timerIntFlag) {//割り込みタイマによる開始まち
      }

      //現在時刻の表示
      sprintf(buf,"%d/%02d/%02d ", d_year+2000, d_month, d_day);
      Serial.print(buf);
      sprintf(buf,"%02d:%02d:%02d ", d_hour, d_min, ss);
      Serial.print(buf);
      sprintf(buf," (%02d/%02d/%02d ", YY, MM, DD);
      Serial.print(buf);
      sprintf(buf,"%02d:%02d:%02d)\n", hh, mm, ss);
      Serial.print(buf);

      if( ss == 0){
        segLED_update();
      }


      // スキャン開始
      I2CwriteByte(I2C_SLAVE_ADDRESS, 4, 1);// LCDのcolon表示をON
      scanbit = 0;
      
      delay(350);
      scanbit += digitalRead(PIN); //@350msec

      scanbit <<= 1;
      delay(100);
      scanbit += digitalRead(PIN); //@450msec

      scanbit <<= 1;
      delay(100);
      scanbit += digitalRead(PIN); //@550msec
  
      scanbit <<= 1;
      delay(100);
      scanbit += digitalRead(PIN); //@650msec

      I2CwriteByte(I2C_SLAVE_ADDRESS, 4, 0);// LCDのcolon表示をOFF
      delay(250);
      

      sprintf(buf,"scanbit=%x, ", scanbit);
      Serial.println(buf);

      timerIntFlag = false;//処理が終わったらフラグをリセットし、次の割り込み発生を待つ

      switch( scanbit ) {// スキャンしたビット列パターンからH/L/Mを判定する。
        case 0:  // 0000
          ret_code = 2;//マーカー
          break;
        case 0x4:// 0100
        case 0x8:// 1000
        case 0xA:// 1010
        case 0xC:// 1100(理想パターン)
        case 0xE:// 1110
          ret_code = 1;// H
          break;
        case 0x7:// 0111
        case 0x9:// 1001
        case 0xB:// 1011
        case 0xD:// 1101
        case 0xF:// 1111(理想パターン)
          ret_code = 0;// L
        break;
        default:
          ret_code = 3;// エラーコード
          break;
      }
      ret_code = (JJYpulseFeed > 0)? ret_code : 3;//JJY信号パルスが止まっているときはエラーコードにする。
      sprintf(buf,"*** CODE=%d", ret_code);
      Serial.println(buf);

      return(ret_code);
}

//内部カウント時計のインクリメント処理 **************************************
void internalClockIncrement(){
  ss++;//次の時刻へ一秒進める
  ss %= 60;
  if( ss == 0 ){
    mm++;

    if( mm == 60 ){
      mm = 0;
      hh++;

      if( hh == 24 ){
        hh = 0;
        DD++;
    
        if( DD > month_day[MM] ){//本当は閏年に２月の日数の考慮が必要
          DD = 1;
          MM++;

          if( MM == 13 ){
            MM = 1;
            YY++;
          }
        }
      }
    }
  }
}



//デコードのためのルーチン **********************************************
void decode() {
  uint16_t longDayCount, dayCount;
  int8_t  bitcount = 0;
  int8_t  bitcode[10];
  uint8_t n;
  bool  codeOk, longDayOk;
  uint8_t h_parity, m_parity, PA1, PA2;

  markerOkCount = 0;
  
  //*** 分のデコード はじめ ***********************
  for (int8_t i = 0; i < 8; i++) {
    n = get_code();
    codeOk = (n == 0 || n == 1)? true : false;
    bitcode[i] = n;
  }

  if( codeOk ) {
    d_min = bitcode[0]*40 + bitcode[1]*20 + bitcode[2]*10
          + bitcode[4]*8  + bitcode[5]*4  + bitcode[6]*2  + bitcode[7];

    m_parity = (bitcode[0]+bitcode[1]+bitcode[2]+bitcode[4]+bitcode[5]+bitcode[6]+bitcode[7]) % 2;
    minCodeHealthy = true;
  }else{
    minCodeHealthy = false;
  }

  if ( get_code() == 2 ){
    Serial.println("Position Marker P1 detected.");
    markerOkCount++;
  }
  else {
    Serial.println("Failed to read Position Maker P1");
  }
  //*** 分のデコード おわり


  //*** 時のデコード はじめ ***********************
  for (int8_t i = 0; i < 9; i++) {
    n = get_code();
    codeOk = (n == 0 || n == 1)? true : false;
    bitcode[i] = n;
  }

  if( codeOk ) {
    d_hour  = bitcode[2]*20 + bitcode[3]*10
            + bitcode[5]*8  + bitcode[6]*4  + bitcode[7]*2  + bitcode[8];

    h_parity = (bitcode[2]+bitcode[3]+bitcode[5]+bitcode[6]+bitcode[7]+bitcode[8]) % 2;
    hourCodeHealthy = true;
  }else{
    hourCodeHealthy = false;
  }

  if( get_code() == 2 ){
    Serial.println("Position Marker P2 detected.");
  }else {
    Serial.println("Failed to read Position Maker P2");
  }
  //*** 時のデコード おわり


  // 通算日数（前半）のデコード はじめ ***********************
  for (int8_t i = 0; i < 9; i++) {
    n = get_code();
    codeOk = (n == 0 || n == 1)? true : false;
    bitcode[i] = n;
  }

  if( codeOk ) {
    longDayCount  = bitcode[2]*200 + bitcode[3]*100
                  + bitcode[5]*80  + bitcode[6]*40  + bitcode[7]*20  + bitcode[8]*10;
    longDayOk = true;
  }
  
  if ( get_code() == 2 ){
    Serial.println("Position Marker P3 detected.");
    markerOkCount++;
  }
  else {
    Serial.println("Failed to read Position Maker P3");
  }
  // 通算日数（前半）のデコード おわり


  // 通算日数（後半）のデコード はじめ ***********************
  for (int8_t i = 0; i < 9; i++) {
    n = get_code();
    codeOk = (n == 0 || n == 1)? true : false;
    bitcode[i] = n;
  }

  if( codeOk && longDayOk ) {
    dayCount = bitcode[0]*8 + bitcode[1]*4 + bitcode[2]*2 + bitcode[3];
    dayCount += longDayCount;

    PA1 = bitcode[6];// 時パリティビット
    PA2 = bitcode[7];// 分パリティビット

    dateCodeHealthy = true;
  }else{
    dateCodeHealthy = false;
  }

  if( get_code() == 2 ){
    Serial.println("Position Marker P4 detected.");
    markerOkCount++;
  }
  else {
    Serial.println("Failed to read Position Maker P4");
  }
  // 通算日数（後半）のデコード おわり

  //通算日数から月、日の算出
  d_month = 0;
  do{
    dayCount -= month_day[d_month];//dayCountから経過月の日数を減算
    d_month++;
  } while( dayCount > month_day[d_month]);//最後の経過月に到達するまで
  d_day = dayCount;

  
  // 年のデコードはじめ  ***********************
  for (int8_t i = 0; i < 9; i++) {
   n = get_code();
   codeOk = (n == 0 || n == 1)? true : false;
   bitcode[i] = n;
  }

  if( codeOk ) {
   d_year  = bitcode[1]*80 + bitcode[2]*40 + bitcode[3]*20
           + bitcode[4]*10 + bitcode[5]*8  + bitcode[6]*4
           + bitcode[7]*2  + bitcode[8];
   yearCodeHealthy = true;       
  }else{
   yearCodeHealthy = false;
  }
    
  if( get_code() == 2 ){
    Serial.println("Position Marker P5 detected.");
  }
  else {
    Serial.println("Failed to read Position Maker P5");
  }
  // 年のデコードおわり


  // 曜日のデコードはじめ  ***********************
  for (int8_t i = 0; i < 9; i++) {
    n = get_code();
    codeOk = (n == 0 || n == 1)? true : false;
    bitcode[i] = n;
  }

  if( codeOk ) {
    d_week  = bitcode[0]*4 + bitcode[1]*2 + bitcode[2];
  }
  
  if( get_code() == 2 ){
    Serial.println("Position Marker P0 detected.");
  }
  else {
    Serial.println("Failed to read Position Maker P0");
  }
  // 曜日のデコードおわり
  
  Serial.println("End of time code decording sequence.");

  markerCheckOk = (markerOkCount > 1)? true : false;//ポジションマーカー検出が2回以上できていればOKとする。

  if ( get_code() == 2 ){
    Serial.println("Marker M detected.");
  }
  else {
    Serial.println("Failed to read Position Maker M");
  }

  if( PA1 == h_parity ){
    Serial.println("Hour parity check OK.");
    HparityCheckOk = true;
  }else {
    Serial.println("Hour parity check NG!!!!");
    HparityCheckOk = false;
  }

  if( PA2 == m_parity ){
    Serial.println("Minute parity check OK.");
    MparityCheckOk = true;
  }else {
    Serial.println("Minute parity check NG!!!!");
    MparityCheckOk = false;
  }

}

//7segLED表示のアップデート **********************************************
void segLED_update() {
  // LEDに時:分の表示
  I2CwriteByte(I2C_SLAVE_ADDRESS, 0, mm % 10);
  I2CwriteByte(I2C_SLAVE_ADDRESS, 1, mm / 10);
  I2CwriteByte(I2C_SLAVE_ADDRESS, 2, hh % 10);
  I2CwriteByte(I2C_SLAVE_ADDRESS, 3, hh / 10);
}




//************************************************************
// 初期設定
//************************************************************
void setup() {
  Serial.begin(115200);
  delay(200);

  //GPIO設定
//  pinMode(LED, OUTPUT);
  pinMode(PIN, INPUT);

  //I2C通信初期設定
  Wire.begin();  
  
  synchronizer_setup();
  
}

//************************************************************
// メインループ 
// [1] タイムコードフレーム同期(2回連続マーカーの検出ループ）
// [2] 1秒毎にパルスコードを読み込んで、デコードし、
//     59秒分のデコード結果が正しそうか判定を行い、
//     判定がOKであればデコード結果を内部カウント時計に反映する。
//     もし、フレーム同期が外れていればこのループを抜けて1.から繰り返す。
//************************************************************
void loop(){
  
  int8_t m, n, p;
  int8_t sec, min = -1, hur;
  bool YYdecodeOk, MMdecodeOk, DDdecodeOk, hhdecodeOk, mmdecodeOk;
  static int8_t YYdecodeOkCount = 0;
  static int8_t MMdecodeOkCount = 0;
  static int8_t DDdecodeOkCount = 0;
  static int8_t hhdecodeOkCount = 0;
  static int8_t mmdecodeOkCount = 0;  
  static uint8_t YYp, MMp, DDp, hhp, mmp;
  char buf[60];

  
  //[1]2回連続マーカーの検出ループ***********************************
  do {
    Serial.println("Looking for Marker");
    p = m;
    m = get_code();

  }while( p * m != 4 );// マーカー(2)が２回続くまで繰り返す。

  Serial.println("2-markers detected!!\n");
  ss = 00;//ここで正分(00秒） にセット


   //[2]デコードを実行するループ ***********************************************
  do {
      //ポジションマーカーP1～P4のうち、2個以上を検出できている間はフレーム同期できているとみなしてループを繰り返す。
      //検出できていなかったらループを終了してマーカー検出からやり直す。

    //segLED_update();
    //デコード実行（60秒間のスキャンとデコード）
    decode();

    sprintf(buf,"Previous decode: %d/%02d/%02d %02d:%02d\n",YYp,MMp,DDp,hhp,mmp);
    Serial.print(buf);
    sprintf(buf,"Current  decode: %d/%02d/%02d %02d:%02d\n",d_year,d_month,d_day,d_hour,d_min);
    Serial.print(buf);

    // Yearのデコード結果チェック
    if( d_year == YYp && yearCodeHealthy ){
      YYdecodeOkCount = (YYdecodeOkCount < 2)? YYdecodeOkCount + 1 : 2;
    }else {
      YYdecodeOkCount = (YYdecodeOkCount > 0)? YYdecodeOkCount - 1 : 0;
    }

    // Monthのデコード結果チェック
    if( d_month == MMp && dateCodeHealthy ){
      MMdecodeOkCount = (MMdecodeOkCount < 2)? MMdecodeOkCount + 1 : 2;
    }else {
      MMdecodeOkCount = (MMdecodeOkCount > 0)? MMdecodeOkCount - 1 : 0;
    }

    // Dayのデコード結果チェック
    if( d_day == DDp && dateCodeHealthy ){
      DDdecodeOkCount = (DDdecodeOkCount < 2)? DDdecodeOkCount + 1 : 2;
    }else {
      DDdecodeOkCount = (DDdecodeOkCount > 0)? DDdecodeOkCount - 1 : 0;
    }
    
    // hourのデコード結果のチェック（前回デコード結果との比較）
    if( d_hour == hhp && hourCodeHealthy && HparityCheckOk ){
        hhdecodeOkCount = (hhdecodeOkCount < 2)? hhdecodeOkCount + 1 : 2;// チェックOKのカウント数の上限を２にする
    }else {
        hhdecodeOkCount = (hhdecodeOkCount > 0)? hhdecodeOkCount - 1 : 0;
    }

    // minのデコード結果のチェック（前回デコード結果との比較）
    if( d_min == (mmp + 1)%60 && minCodeHealthy && MparityCheckOk){
        mmdecodeOkCount = (mmdecodeOkCount < 2)? mmdecodeOkCount + 1 : 2;// チェックOKのカウント数の上限を２にする
    }else {
        mmdecodeOkCount = (mmdecodeOkCount > 0)? mmdecodeOkCount - 1 : 0;
    }
    

    //2回以上連続で比較が一致すればOKとする
    YYdecodeOk = (YYdecodeOkCount > 1)? true : false;
    MMdecodeOk = (MMdecodeOkCount > 1)? true : false;
    DDdecodeOk = (DDdecodeOkCount > 1)? true : false;
    hhdecodeOk = (hhdecodeOkCount > 1)? true : false;
    mmdecodeOk = (mmdecodeOkCount > 1)? true : false;
    
    // 今回のデコード結果を保存
    YYp = d_year;
    MMp = d_month;
    DDp = d_day;
    hhp = d_hour;
    mmp = d_min;


    //デコードチェックに応じてデコード結果を内部カウント時計に反映する
    if( mmdecodeOk ){//分デコード結果を反映
      mm = d_min + 1;
      if( mm == 60 ){
        mm = 0;
        hh = d_hour + 1;

        if( hh == 24 ){
          hh = 0;
          DD++;

          if( DD > month_day[MM] ){
            DD = 1;
            MM++;

            if( MM == 13 ){
              MM = 1;
              YY++;
            }
          }
        }
      }
    }

    if( YYdecodeOk ){//Yearデコード結果の反映。
        YY = d_year;
    }

    if( MMdecodeOk ){//Monthデコード結果の反映。月をまたぐときは反映すべきでないが。
        MM = d_month;
    }

    if( DDdecodeOk && (hh != 0 || mm != 0) ){ //Dayデコード結果の反映。日をまたぐときは反映しない。
        DD = d_day;
    }

    if( hhdecodeOk && (mm != 0) ){//時デコード結果の反映。毎正時のときは反映しない
        hh = d_hour;
    }

    //segLED_update();//デコード反映結果を表示する
    sprintf(buf,"******* %d/%02d/%02d ", 2000 + YY, MM, DD);
    Serial.print(buf);
    sprintf(buf,"%02d:%02d(%d %d %d %d %d)\n", hh, mm, YYdecodeOkCount, MMdecodeOkCount, DDdecodeOkCount, hhdecodeOkCount, mmdecodeOkCount);
    Serial.print(buf);

  }while( markerCheckOk && (JJYpulseFeed > 0) );//マーカー位置が確認できている間は次の1分間のデコードを繰り返す。
                                          //JJYパルスとの同期ができていないときはデコードをしない。



  //JJY信号が止まっているときに内部時計だけで時刻カウントさせるループ
  while( JJYpulseFeed == 0 ){
    n = get_code();

    sprintf(buf,"Lost JJY pulse input!!!(%d)\n", JJYpulseFeed);
    Serial.print(buf);
  }

}
