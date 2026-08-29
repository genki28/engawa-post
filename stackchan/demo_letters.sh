#!/bin/bash
# デモ用サンプルお便り送信。使い方: ./demo_letters.sh happy|sad|warm
case "${1:-happy}" in
happy) D='{"text":"おばあちゃん、聞いて。今日ね、仕事で作ったものが初めて人に褒められたんだよ。一番に知らせたくなりました。","emotion":"happy","highlight":"初めて褒められた"}';;
sad)   D='{"text":"ゆうべ、おじいちゃんの夢を見たよ。少しだけ泣きました。でも、なんだかあったかい朝です。","emotion":"sad","highlight":"おじいちゃんの夢"}';;
warm)  D='{"text":"こっちは元気にやっとるよ。おばあちゃんの茶碗蒸しが恋しい季節になってきました。また岐阜に帰るね。","emotion":"warm","highlight":"茶碗蒸し"}';;
esac
curl -s -X POST http://localhost:8787/api/letter -H 'content-type: application/json' -d "$D"; echo
