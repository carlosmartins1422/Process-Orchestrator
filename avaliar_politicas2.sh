#!/bin/bash
# Cenário pensado para evidenciar diferenças entre FIFO, RANDOM e FAIR
# quando alguns utilizadores submetem poucos jobs e outros muitos.

REPORT="avaliacao_politicas2.txt"
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

monitoriza_user() {
	local user_id="$1"
	local inicio_user="$2"
	shift 2

	while true; do
		local ativo=0
		for pid in "$@"; do
			if kill -0 "$pid" 2>/dev/null; then
				ativo=1
				break
			fi
		done

		if [ "$ativo" -eq 0 ]; then
			break
		fi

		sleep 0.05
	done

	local fim_user tempo_user
	fim_user=$(date +%s.%N)
	tempo_user=$(calcula_tempo "$inicio_user" "$fim_user")
	echo "User $user_id | Tempo total desde a primeira submissão até à conclusão do último processo: $tempo_user segundos" >> tmp/tempos_users.txt
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

resume_tempos_users() {
	echo >> "$REPORT"
	echo "[Tempo total por utilizador]" >> "$REPORT"

	if [ ! -f tmp/tempos_users.txt ]; then
		echo "(tempos por utilizador não disponíveis)" >> "$REPORT"
		return
	fi

	cat tmp/tempos_users.txt >> "$REPORT"
}

lanca_runner() {
	local user_id="$1"
	local saida="$2"
	local comando="$3"
	./bin/runner -e "$user_id" "$comando" > "$saida" 2>&1 &
	LAST_PID=$!
}

espera_lista_pids() {
	for pid in "$@"; do
		wait "$pid"
	done
}

executa_teste() {
	local politica="$1"
	local paralelismo="$2"
	local base="tmp/${politica}_${paralelismo}_c2"
	local inicio fim tempo_total
	local user1_pids=()
	local user2_pids=()
	local user3_pids=()
	local user4_pids=()

	rm -f log.txt
	rm -f tmp/*

	echo "==================================================" >> "$REPORT"
	echo "POLITICA: $politica | PARALELISMO: $paralelismo" >> "$REPORT"
	echo "CENARIO: users 1 e 2 com muitos jobs longos; users 3 e 4 com poucos jobs curtos" >> "$REPORT"
	echo "Submissão: users 1 e 2 entram primeiro; users 3 e 4 entram depois" >> "$REPORT"

	inicio=$(date +%s.%N)
	echo "A correr teste: politica=$politica paralelismo=$paralelismo"

	./bin/controller "$paralelismo" "$politica" > /dev/null 2>&1 &
	controller_pid=$!

	sleep 1

	inicio_user1=$(date +%s.%N)
	for i in 1 2 3 4 5 6 7 8; do
		lanca_runner 1 "${base}_u1_c${i}.txt" "sleep 3"
		user1_pids+=("$LAST_PID")
	done
	monitoriza_user 1 "$inicio_user1" "${user1_pids[@]}" &
	monitor1_pid=$!

	inicio_user2=$(date +%s.%N)
	for i in 1 2 3 4 5 6 7 8; do
		lanca_runner 2 "${base}_u2_c${i}.txt" "sleep 3"
		user2_pids+=("$LAST_PID")
	done
	monitoriza_user 2 "$inicio_user2" "${user2_pids[@]}" &
	monitor2_pid=$!

	sleep 1

	inicio_user3=$(date +%s.%N)
	for i in 1 2; do
		lanca_runner 3 "${base}_u3_c${i}.txt" "sleep 1"
		user3_pids+=("$LAST_PID")
	done
	monitoriza_user 3 "$inicio_user3" "${user3_pids[@]}" &
	monitor3_pid=$!

	inicio_user4=$(date +%s.%N)
	for i in 1 2; do
		lanca_runner 4 "${base}_u4_c${i}.txt" "sleep 1"
		user4_pids+=("$LAST_PID")
	done
	monitoriza_user 4 "$inicio_user4" "${user4_pids[@]}" &
	monitor4_pid=$!

	espera_lista_pids "${user1_pids[@]}" "${user2_pids[@]}" "${user3_pids[@]}" "${user4_pids[@]}"
	wait "$monitor1_pid"
	wait "$monitor2_pid"
	wait "$monitor3_pid"
	wait "$monitor4_pid"

	./bin/runner -s > /dev/null 2>&1
	wait "$controller_pid"
	controller_pid=""

	fim=$(date +%s.%N)
	tempo_total=$(calcula_tempo "$inicio" "$fim")

	echo >> "$REPORT"
	echo "[Resumo do teste]" >> "$REPORT"
	echo "Tempo total: $tempo_total segundos" >> "$REPORT"

	resume_log
	resume_tempos_users

	rm -f "${base}"_*.txt
}

echo "AVALIACAO DE POLITICAS DE ESCALONAMENTO - CENARIO 2" > "$REPORT"
echo >> "$REPORT"
echo "Relatório resumido: política, paralelismo, tempo total, tempo de cada comando e tempo total por utilizador." >> "$REPORT"
echo "Objetivo: mostrar o impacto quando alguns users submetem poucos jobs e outros muitos." >> "$REPORT"

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
