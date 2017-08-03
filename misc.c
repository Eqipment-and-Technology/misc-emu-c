#include <stdio.h>
#include <errno.h>
//#include <string.h>
#include <malloc.h>
#include <stdint.h>
#include "codes.h"

#define LINUX
#define DEBUG

#ifdef DEBUG
	#define DEBUGMSG(message) printf(message)
	char* debugString = " Инстр. ";
#else
	#define DEBUGMSG(message)
#endif

#ifdef LINUX
	FILE *rom;
#endif

#define GETHIGH(value) (value & 0b11111111)
#define GETLOW(value) (value >> 8)

typedef uint8_t		byte;
typedef uint16_t	word;

// Структура ЦП
typedef struct {
	word ac; // аккумулятор
	byte carry; // флаг переноса
	word pc; // программный счетчик
} cpu_struct;
cpu_struct cpu;
// cpu.ac = 0xFF00; адрес после сброса

word *ram; // ОЗУ виртуальной машины

/**
 * @brief получает байт из памяти
 * @returns возвращает считанный байт
 * 
 * 
 */
int inline getByte(){
#ifdef LINUX
	return getc(rom);
#endif
}

/**
 * @brief функция, выполняющая расширенные (EXT) инструкции процессора.
 * @param low младший байт инструкции
 * @param high старший байт инструкции
 * @returns 1 в случае вызова инструкции HALT, 0 в остальных случаях 
 * 
 * 
 */
int extended(byte low, byte high){
	if ((low >> 4) & 1){
		// набор 1
		/*		 3 2	1 0 (на сколько надо сдвигать)
			1111 LOW/HIGH NOTA AND/ADD NOTAL, XXXXXXXX
			D: 0 - LOW, младший байт, 1 - HIGH, старший)
			F: 0 - AND, 1 - ADD 
		*/
		byte destination = 0;
		char *_ac = (char *) &cpu.ac;
		DEBUGMSG("Набор EXT 1: ");
		if(low>>3 & 1){
			DEBUGMSG("HIGH ");
			destination = 1;
		}else{
			DEBUGMSG("LOW ");
		}
		if(low>>2 & 1){
			DEBUGMSG("NOTA ");
			cpu.ac=~cpu.ac; //Not accumulator
		}
		if(low>>1 & 1){
			DEBUGMSG("ADDC");
			if(destination){
				if ( (long int) (_ac[1] + high) >= 0x1000 )
					cpu.carry = 1;
				else
					cpu.carry = 0;
				_ac[1] += high; // прибавляем к старшему байту аккумулятора константу (high)
			}
			else{
				if ( (long int) (_ac[0] + high) >= 0x1000 )
					cpu.carry = 1;
				else
					cpu.carry = 0;
				_ac[0] += high; // прибавляем к младшему байту аккумулятора константу
			}
		}else{
			DEBUGMSG("ANDC");
			if(destination)	_ac[1] &= high; // эндим старший байт аккумулятора с константой
			else			_ac[0] &= high; // эндим младший байт аккумулятора с константой
		}
		if(low & 1){
			DEBUGMSG("NOTAL ");
			cpu.ac=~cpu.ac; //Not accumulator (last)
		}
		DEBUGMSG("\n");
	} else {
		// набор 0
		/*		 3	  2	   1	 0	  7    654 3	2  1  0
			1110 NOTA INCA NOTAL SHL, SWAP XXX SKPC EI DI HALT
		*/
		DEBUGMSG("Набор EXT 0: ");
		if(low>>3 & 1){
			DEBUGMSG("NOTA ");
			cpu.ac=~cpu.ac; // NOT Accumulator
		}
		if(low>>2 & 1){
			DEBUGMSG("INCA ");
			// установка флага переноса
			if( (long int) cpu.ac >= 255 )
				cpu.carry=1;
			else
				cpu.carry=0;
			++cpu.ac; //INCrement Accumulator
		}
		if(low>>1 & 1){
			DEBUGMSG("NOTAL ");
			cpu.ac=~cpu.ac; // NOT Accumulator
		}
		if(low & 1){
			DEBUGMSG("SHLC ");
			if((high>>4) & 7){
				cpu.ac |= cpu.carry << 15;
				cpu.ac = cpu.ac<<( (high>>4) & 7 ); // SHift Left with Carry
			}
				cpu.carry = cpu.ac >> 15;
		}else{
			DEBUGMSG("SHL ");
			cpu.ac=cpu.ac<<( (high>>4) & 7 ); // SHift Left without Carry
		}
		// обработка старшего байта
		if(high>>7 & 1){
			DEBUGMSG("SWAP ");
			cpu.ac=(cpu.ac<<8)|(cpu.ac>>8); // SWAP
		}
		if(high>>3 & 1){
			DEBUGMSG("SKC ");
			if(cpu.carry) cpu.pc++; // SKip if Carry
		}
		if(high>>2 & 1){
			DEBUGMSG("EI ");
			// TODO EI
		}
		if(high>>1 & 1){
			DEBUGMSG("DI ");
			// TODO DI
		}
		if(high & 1){
			DEBUGMSG("HALT ");
			return 1;
		}
		DEBUGMSG("\n");
	}
	return 0;
}

/**
 * @brief эмулятор процессора, загружает ПЗУ в ОЗУ из файла, декодирует инструкцию, 
 * @returns 1 в случае ошибки, 0 в случае успешного завершения процесса эмуляции
 * 
 * 
 */
int emulator(unsigned int memsize, byte mode){
	/* загрузка в ОЗУ содержимого ПЗУ */
	unsigned int i;
	for(i=0; i<memsize; i++){ // загрузка ПЗУ в ОЗУ
		int lowBuf = getByte(); // для начала надо прочитать целые значения и проверить их на EOF
		int highBuf = getByte();
		if( lowBuf == EOF || highBuf == EOF ) // файл закончился
			break;
		byte low = (byte) lowBuf; // если все хорошо, то перевести считанное в байты
		byte high = (byte) highBuf;
		ram[i] = (low << 8) | high;
	};
#ifdef DEBUG
	word breakaddr = 0;
	// обработка точек останова: если дошли до нужного адреса, включаем режим
	if(cpu.pc == breakaddr)
		mode = 1;
#endif
	/* декодирование и выполнение инструкций */
	while(1){
		byte high = GETHIGH(ram[cpu.pc]);
		byte low = GETLOW(ram[cpu.pc]);
		byte instruction = low >> 5; // номер инструкции
		/* извлечение аргумента и определение метода адресации */
		byte page = 0, inderect, sign; // бит №3, №4
		word arg = 0, data = 0;
		if(instruction<6){ // для инструкций INOUT и EXT обработка режимов адресации не нужна
			page = (low >> 4) & 1; // бит №3
			inderect = (low >> 3) & 1;
			sign = (low >> 2) & 1;
			arg = ((low & 7) << 8) | high;
			// CCCP-IS## ####-#### (instruction Code, Page, Inderect, Sign of relative addresing mode, # - raw arg)
			if(page){
				if(sign)
					data=cpu.pc + (arg & 0x3FF); //прибавляем к текущему адресу аргумент кроме бита знака
				else
					data=cpu.pc - arg; //вычитаем из текущего адреса аргумент
			}else
				data=arg;
			if(inderect)
				data = ram[data]; //суем это слово в адрес и снова по этому адресу берем слово
		}
#ifdef DEBUG
		/* вывод отладочной информации */
		printf("Байт (%X : %X) на смещении %u.\n", low, high, cpu.pc);
		printf("Cтраница %u, косвенная адресация %u. Считанный аргумент %X, вычисленный аргумент %X.", page, inderect, arg, data);
#endif
		/* декодирование и выполнение инструкции */
		switch(instruction){
			default:
				return 1; // это возможно, только если таблица с кодами команд нарушена
			case AND:
				DEBUGMSG(" AND\n");
				cpu.ac &= ram[data];
				break;
			case ADD:
				DEBUGMSG(" ADD\n");
				// сложение -- единственная инструкция в основном наборе, которая выставляет флаг переноса
				if ( (long int) (cpu.ac + ram[data]) >= 0x10000 )
					cpu.carry = 1;
				else
					cpu.carry = 0;
				cpu.ac += ram[data];
				break;
			case SCA:
				DEBUGMSG(" SCA\n"); // Store and Clear Accumulator
				ram[data] = cpu.ac; // сохранить А по адресу ARG в ОЗУ
				cpu.ac = 0; // очистить А
				break;
			case INZ:
				DEBUGMSG(" INZ\n");
				ram[data]++; // INcrement (word in memory) and
				if(!ram[data]) cpu.pc++; // skip next instruction if result is Zero
				break;
			case JMP:
				DEBUGMSG(" JMP\n");
				cpu.pc = data; // установить новое значение
				continue; // избежать инкремента адреса
				break;
			case CALL:
				DEBUGMSG(" CALL\n");
				ram[data] = cpu.pc+1; // сохранить адрес возврата в ОЗУ
				cpu.pc = data; // перейти на адрес аргумент+1 (инкремент в конце цикла)
				break;
			case INOUT:
				DEBUGMSG(" INOUT\n");
				// TODO полноценная обработка ввода-вывода
				// 110D-####-####-#### (D - direction)
				long int dev = (low & 0xF) | high;
				int direction = (low >> 4) & 1;
#ifdef DEBUG
				printf("%s устройство %i\n", (direction?"Вывод в":"Ввод на"), dev);
#endif
				break;
			case EXT:
				DEBUGMSG(" EXT\n");
				if (extended(low, high)) // вернули 1 - инструкция HALT
					return 0;
				break;
		}
#ifdef DEBUG
		printf("A=%X; C=%u\n", (unsigned int) cpu.ac, (unsigned int) cpu.carry);
		if(mode){
			char command, tmp;
			int from, to, i;
			while(1){
				printf("DBG>");
				command=getchar();
				if(command>='A' && command<='Z') command+=32; 
				switch(command){
					case 'b': // Breakpoint
						scanf("%d", &breakaddr);
						tmp=getchar();
						break;
					case 'c': // Cont
						mode = 0;
						continue;
						break;
					case 'd': // Dump
						scanf("%d%d", &from, &to);
						tmp=getchar();
						for (i=from; i<to; i++) printf("%X %X:%X\n", (unsigned char) i, (unsigned char) ((char *) &ram[i])[1], (unsigned char) ((char *) &ram[i])[0]);
						break;
					case 'n': // Next
						tmp=getchar();
						goto next;
						break;
					case 'q':
						tmp=getchar();
						return 0;
					case 's':
						scanf("%d%d", &from, &to);
						tmp=getchar();
						ram[from]=to;
					default: // error
						tmp=getchar();
						printf("Ошибка!\n");
						break;
				}
			}
		}
		next:
#endif
		cpu.pc++;
	}

	return 0;
}

int main(int argc, char **argv){
	
	// 0	1		2	3	4		5
	// misc	rom.bin	256	-o	ram.bin	-D
	if (argc < 3){
		printf("Не достаточно рагументов!\n");
		return 1;
	}
	int memsize = atoi(argv[2]);
	if (!memsize){
		printf("Неверный размер ОЗУ!\n");
		return 1;
	}
	if( !(ram = (word*) malloc( memsize )) ){
		printf("Не могу получить память под вирт. ОЗУ!\n");
		return 1;
	}
	// открытие файла
	rom = fopen(argv[1], "rb");
	if (rom == NULL){
		printf("Не могу открыть файл %s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	
#ifdef DEBUG
	byte mode = 0; // No debug as default
	if( !strcmp(argv[5], "-D") ) // 
		mode = 1; // 
#endif
	
	if (emulator(memsize, mode))
		printf("Ошибка эмулятора!\n");
	else
		printf("Эмулятор завершил работу нормально.\n");
	
	/* сохраняем содержимое ОЗУ в файл */
	if( !strcmp(argv[3], "-o") ){
		FILE *fout = fopen(argv[4], "wb");
		int i;
		for(i=0; i<memsize; i++)
			fprintf(fout, "%c%c", ((char *) &ram[i])[1], ((char *) &ram[i])[0] );
		fclose(fout);
	}
	
	free(ram);
	
	return 0;
}
