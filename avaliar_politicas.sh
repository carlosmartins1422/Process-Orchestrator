#!/bin/bash
# antes de correr ./avaliar_politicas.sh
# tem que se fzer chmod +x avaliar_politicas.sh

REPORT="avaliacao_politicas.txt"
controller_pid=""
LAST_PID=""

cleanup() {
	if [ -n "$controller_pid" ]; then
		kill "$controller_pid" 2>/dev/null || true
	fi
}

trap cleanup EXIT

calcula_tempo() {
	local inicio="$1"
	local fim="$2"
	awk -v inicio="$inicio" -v fim="$fim" 'BEGIN { printf "%.2f", fim - inicio }'
}

resume_log() {
	echo >> "$REPORT"
	echo "[Comandos executados]" >> "$REPORT"

	if [ ! -f log.txt ]; then
		echo "(log.txt não foi criado)" >> "$REPORT"
		return
	fi

	awk '
		/^Utilizador:/ {
			user = $2
			next
		}
		/^Comando executado:/ {
			cmd = substr($0, index($0, ":") + 2)
		}
		/^Tempo gasto:/ {
			printf "User %s | %s | %s s\n", user, cmd, $3
		}
	' log.txt >> "$REPORT"
}

lanca_runner() {
	local user_id="$1"
	local saida="$2"
	shift 2
	./bin/runner -e "$user_id" "$@" > "$saida" 2>&1 &
	LAST_PID=$!
}

executa_teste() {
	local politica="$1"
	local paralelismo="$2"
	local base="tmp/${politica}_${paralelismo}"
	local inicio
	local fim
	local tempo_total

	rm -f log.txt
	rm -f tmp/*

	echo "==================================================" >> "$REPORT"
	echo "POLITICA: $politica | PARALELISMO: $paralelismo" >> "$REPORT"
	echo "CENARIO: 4 utilizadores, 5 comandos por utilizador" >> "$REPORT"

	inicio=$(date +%s.%N)
	echo "A correr teste: politica=$politica paralelismo=$paralelismo"

	./bin/controller "$paralelismo" "$politica" > /dev/null 2>&1 &
	controller_pid=$!

	sleep 1

	lanca_runner 1 "${base}_u1_c1.txt" sleep 3
	runner1_pid=$LAST_PID
	lanca_runner 1 "${base}_u1_c2.txt" sleep 3
	runner2_pid=$LAST_PID
	lanca_runner 1 "${base}_u1_c3.txt" sleep 3
	runner3_pid=$LAST_PID
	lanca_runner 1 "${base}_u1_c4.txt" sleep 3
	runner4_pid=$LAST_PID
	lanca_runner 1 "${base}_u1_c5.txt" sleep 3
	runner5_pid=$LAST_PID

	sleep 1

	lanca_runner 2 "${base}_u2_c1.txt" sleep 1
	runner6_pid=$LAST_PID
	lanca_runner 2 "${base}_u2_c2.txt" sleep 1
	runner7_pid=$LAST_PID
	lanca_runner 2 "${base}_u2_c3.txt" sleep 1
	runner8_pid=$LAST_PID
	lanca_runner 2 "${base}_u2_c4.txt" sleep 1
	runner9_pid=$LAST_PID
	lanca_runner 2 "${base}_u2_c5.txt" sleep 1
	runner10_pid=$LAST_PID

	sleep 1

	lanca_runner 3 "${base}_u3_c1.txt" sleep 2
	runner11_pid=$LAST_PID
	lanca_runner 3 "${base}_u3_c2.txt" sleep 2
	runner12_pid=$LAST_PID
	lanca_runner 3 "${base}_u3_c3.txt" sleep 2
	runner13_pid=$LAST_PID
	lanca_runner 3 "${base}_u3_c4.txt" sleep 2
	runner14_pid=$LAST_PID
	lanca_runner 3 "${base}_u3_c5.txt" sleep 2
	runner15_pid=$LAST_PID

	sleep 1

	lanca_runner 4 "${base}_u4_c1.txt" sleep 1
	runner16_pid=$LAST_PID
	lanca_runner 4 "${base}_u4_c2.txt" sleep 3
	runner17_pid=$LAST_PID
	lanca_runner 4 "${base}_u4_c3.txt" sleep 1
	runner18_pid=$LAST_PID
	lanca_runner 4 "${base}_u4_c4.txt" sleep 3
	runner19_pid=$LAST_PID
	lanca_runner 4 "${base}_u4_c5.txt" sleep 1
	runner20_pid=$LAST_PID

	wait "$runner1_pid"
	wait "$runner2_pid"
	wait "$runner3_pid"
	wait "$runner4_pid"
	wait "$runner5_pid"
	wait "$runner6_pid"
	wait "$runner7_pid"
	wait "$runner8_pid"
	wait "$runner9_pid"
	wait "$runner10_pid"
	wait "$runner11_pid"
	wait "$runner12_pid"
	wait "$runner13_pid"
	wait "$runner14_pid"
	wait "$runner15_pid"
	wait "$runner16_pid"
	wait "$runner17_pid"
	wait "$runner18_pid"
	wait "$runner19_pid"
	wait "$runner20_pid"

	./bin/runner -s > /dev/null 2>&1
	wait "$controller_pid"
	controller_pid=""

	fim=$(date +%s.%N)
	tempo_total=$(calcula_tempo "$inicio" "$fim")

	echo >> "$REPORT"
	echo "[Resumo do teste]" >> "$REPORT"
	echo "Tempo total: $tempo_total segundos" >> "$REPORT"

	resume_log

	rm -f "${base}"_*.txt
}

echo "AVALIACAO DE POLITICAS DE ESCALONAMENTO" > "$REPORT"
echo >> "$REPORT"
echo "Relatório resumido: política, paralelismo, tempo total e tempo de cada comando." >> "$REPORT"

make clean > /dev/null 2>&1
make > /dev/null 2>&1

executa_teste FIFO 2
executa_teste RANDOM 2
executa_teste FAIR 2
executa_teste FIFO 4
executa_teste RANDOM 4
executa_teste FAIR 4

echo >> "$REPORT"
echo "==================================================" >> "$REPORT"
echo "FIM DOS TESTES" >> "$REPORT"

echo "Testes concluídos. Consulta o ficheiro $REPORT"
