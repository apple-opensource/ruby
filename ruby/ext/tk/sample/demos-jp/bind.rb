#
# text (tag bindings) widget demo (called by 'widget')
#

# toplevel widget が存在すれば削除する
if defined?($bind_demo) && $bind_demo
  $bind_demo.destroy 
  $bind_demo = nil
end

# demo 用の toplevel widget を生成
$bind_demo = TkToplevel.new {|w|
  title("Text Demonstration - Tag Bindings")
  iconname("bind")
  positionWindow(w)
}

# frame 生成
TkFrame.new($bind_demo) {|frame|
  TkButton.new(frame) {
    #text '了解'
    text '閉じる'
    command proc{
      tmppath = $bind_demo
      $bind_demo = nil
      tmppath.destroy
    }
  }.pack('side'=>'left', 'expand'=>'yes')

  TkButton.new(frame) {
    text 'コード参照'
    command proc{showCode 'bind'}
  }.pack('side'=>'left', 'expand'=>'yes')
}.pack('side'=>'bottom', 'fill'=>'x', 'pady'=>'2m')

# bind 用メソッド
def tag_binding_for_bind_demo(tag, enter_style, leave_style)
  tag.bind('Any-Enter', proc{tag.configure enter_style})
  tag.bind('Any-Leave', proc{tag.configure leave_style})
end

# text 生成
TkText.new($bind_demo){|t|
  # 生成
  setgrid 'true'
  width  60
  height 24
  font $font
  wrap 'word'
  TkScrollbar.new($bind_demo) {|s|
    pack('side'=>'right', 'fill'=>'y')
    command proc{|*args| t.yview(*args)}
    t.yscrollcommand proc{|first,last| s.set first,last}
  }
  pack('expand'=>'yes', 'fill'=>'both')

  # スタイル設定
  if TkWinfo.depth($root).to_i > 1
    tagstyle_bold = {'background'=>'#43ce80', 'relief'=>'raised', 
                     'borderwidth'=>1}
    tagstyle_normal = {'background'=>'', 'relief'=>'flat'}
  else
    tagstyle_bold = {'foreground'=>'white', 'background'=>'black'}
    tagstyle_normal = {'foreground'=>'', 'background'=>''}
  end

  # テキスト挿入
  insert 'insert', "テキストwidgetの表示スタイルを制御するのと同じタグのメカニズムを使って、テキストにTclのコマンドを割り当てることができます。これにより、マウスやキーボードのアクションで特定のTclのコマンドが実行されるようになります。例えば、下のキャンバスのデモプログラムについての説明文にはそのようなタグがついています。マウスを説明文の上に持っていくと説明文が光り、ボタン1を押すとその説明のデモが始まります。

"
  insert('end', '1. キャンバス widget に作ることのできるアイテムの種類全てに関するサンプル。', (d1 = TkTextTag.new(t)) )
  insert('end', "\n\n")
  insert('end', '2. 簡単な 2次元のプロット。データを表す点を動かすことができる。', (d2 = TkTextTag.new(t)) )
  insert('end', "\n\n")
  insert('end', '3. テキストアイテムのアンカーと行揃え。', 
         (d3 = TkTextTag.new(t)) )
  insert('end', "\n\n")
  insert('end', '4. ラインアイテムのための矢印の頭の形のエディタ。', 
         (d4 = TkTextTag.new(t)) )
  insert('end', "\n\n")
  insert('end', '5. タブストップを変更するための機能つきのルーラー。', 
         (d5 = TkTextTag.new(t)) )
  insert('end', "\n\n")
  insert('end', 
         '6. キャンバスがどうやってスクロールするのかを示すグリッド。', 
         (d6 = TkTextTag.new(t)) )

  # binding
  [d1, d2, d3, d4, d5, d6].each{|tag|
    tag_binding_for_bind_demo(tag, tagstyle_bold, tagstyle_normal)
  }
  d1.bind('1', 
          proc{eval `cat #{[$demo_dir,'items.rb'].join(File::Separator)}`})
  d2.bind('1', 
          proc{eval `cat #{[$demo_dir,'plot.rb'].join(File::Separator)}`})
  d3.bind('1', 
          proc{eval `cat #{[$demo_dir,'ctext.rb'].join(File::Separator)}`})
  d4.bind('1', 
          proc{eval `cat #{[$demo_dir,'arrow.rb'].join(File::Separator)}`})
  d5.bind('1', 
          proc{eval `cat #{[$demo_dir,'ruler.rb'].join(File::Separator)}`})
  d6.bind('1', 
          proc{eval `cat #{[$demo_dir,'cscroll.rb'].join(File::Separator)}`})

  TkTextMarkInsert.new(t, '0.0')
  configure('state','disabled')
}
