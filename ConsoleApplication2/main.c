#include <stdio.h>
#include <stdlib.h>


// Macros, structures, global variables

#define pageSize 32 // 32 LBA(1 LBA�� ũ��� 512Byte)�� �ϳ��� �������� ������.

typedef struct _Node { // struct Node
	int blkNumber;
	int pageCnt;
	int validCnt;

	struct _Node *prev;
	struct _Node *next;
} Node;

typedef struct _List { // struct doubly-linked list
	int cnt;
	Node *head;
	Node *tail;
} List;


List *freeBlkList = NULL; // free block���� ����Ʈ
List *unfreeBlkList = NULL; // unfree block���� ����Ʈ
Node *openBlk = NULL; // ���� open �Ǿ��ִ� block.
int L2P[1024*2098]; // �� ����� 2048��, ����� �������� 1024��.. �ϳ��� 4Byte�̹Ƿ� table�� ũ��� �� ũ��� 8MB. Logical -> Physical��. ������ 50���� overflow division ����!
int P2L[1024*2098]; // Physical->Logical. ��, �������� �������. 0~1023���� ���ۺ��0��, 1024~2047�� ���ۺ��1��..

//
// List functions
//

List *create_list() { // �� ����Ʈ�� ����.
	List *L = (List *)malloc(sizeof(List));

	L->cnt = 0;
	L->head = L->tail = NULL;

	return L;
}

Node *find_node_with_blkNum(List *L, int num) { // block Number�� ������ ���ϴ� block�� ã��.
	Node *N = L->head;

	if(openBlk != NULL && openBlk->blkNumber == num) {
		return openBlk;
	}

	for(N = L->head; N != NULL; N = N->next) {
		if(N->blkNumber == num) {
			return N;
		}
	}

	return NULL;
}

void add_list_tail(List *L, int newblk) { // list�� tail�� item ����(queue ����)
	Node *N = (Node *)malloc(sizeof(Node));
	
	N->blkNumber = newblk;
	N->pageCnt = 0;
	N->validCnt = 0;
	N->next = NULL;

	if(L->cnt == 0) {
		L->head = L->tail = N;
	}
	else {
		N->prev = L->tail;
		L->tail->next = N;
		L->tail = N;
	}
	L->cnt++;
}

void add_list_tail_node(List *L, Node *N) { // list�� tali�� node ����(queue ����)
	N->next = NULL;

	if(L->cnt == 0) {
		L->head = L->tail = N;
	}
	else {
		N->prev = L->tail;
		L->tail->next = N;
		L->tail = N;
	}
	L->cnt++;
}

Node *remove_head(List *L) { // list�� head item�� ����(queue ����)
	if(L->cnt == 0) {
		printf("ERROR: There is no free block.\n");
		return NULL;		
	}
	else if(L->cnt == 1) {
		Node *tmp = L->head;

		L->head = L->tail = NULL;
		L->cnt--;
		return tmp;
	}
	else {
		Node *tmp = L->head;
		
		tmp->next->prev = NULL;
		L->head = tmp->next;
		tmp->next = NULL;
		L->cnt--;
		return tmp;
	}
}

Node *remove_node(List *L, Node *N) { // list�� ���ϴ� node�� ����
	if(N == L->head && N == L->tail) { // ���� ��尡 head=tail�� ���
		L->head = L->tail = NULL;
		L->cnt--;
		return N;
	}
	else if(N == L->head) { // head�� ���
		N->next->prev = NULL;
		L->head = N->next;
		N->next = NULL;
		L->cnt--;
		return N;
	}
	else if(N == L->tail) { //tail�� ���
		N->prev->next = NULL;
		L->tail = N->prev;
		N->prev = NULL;
		L->cnt--;
		return N;
	}
	else { // �Ѵ� �ƴ� ���
		N->prev->next = N->next;
		N->next->prev = N->prev;

		N->prev = N->next = NULL;
		L->cnt--;
		return N;	
	}
}

Node *delete_item_with_validCnt(List *L, int item) { // list�� ���ϴ� item�� ����.
	Node *tmp;

	for(tmp = L->head; tmp != NULL; tmp = tmp->next) {
		if(tmp->validCnt == item) {
			if(tmp == L->head && tmp == L->tail) { // �� ���� head=tail, �� 1���� item�� �ִ� ���. 
				L->head = L->tail = NULL;
				L->cnt--;
				return tmp;
			}
			else if(tmp == L->head) {
				// ���� node�� head�� ���.
				tmp->next->prev = NULL;
				L->head = tmp->next;
				tmp->next = NULL;
				L->cnt--;
				return tmp;
			}
			else if(tmp == L->tail) { // tail�� ���.
				tmp->prev->next = NULL;
				L->tail = tmp->prev;
				tmp->prev = NULL;
				L->cnt--;
				return tmp;
			}
			else { // head��, tail�� �ƴ� ���
				tmp->prev->next = tmp->next;
				tmp->next->prev = tmp->prev;

				tmp->prev = tmp->next = NULL;
				L->cnt--;
				return tmp;
			}
		}
	}
	return NULL;
}


//
// End List
//


// Write�� well align, misalign���� ������ ���� ���̽����� 1 �������� ���� ���, ���� �������� ���� ���� ����.
// �̹� ������ mapping�� ������ �⺻������ ���� ���� ����ϳ� �ٸ� ���� read-back �� overwrite.
// misalign�� ���� ù �������� read-back�� �� ��, mapping�� ���� ����.

// Read�� �� �������� �д� ���, ���� �������� �д� ���� ����.

// Erase�� ��û�� �������� ������ invalid�� ����(0xFFFFFFFF). ���������� �� �������� erase�ϴ� ���, ���� �������� erase�ϴ� ���� ��������.

// Garbage Collection�� free block�� ���� 20�� ������ �� �����ؼ� 40���� �� ������ �����Ѵ�.
// unfreeblock �߿��� valid page�� ������ ���� ���� block���� ã�Ƽ� victim block���� ������ ��,
// free block�� victim block�� valid page���� �����ϰ�, victim block���� �ٽ� free block�� �Ǿ� write �� �� �ְ� �ȴ�.


void GarbageCollection() { 
	int i;
	printf("Garbage Collection request\n");
	
	while(freeBlkList->cnt < 40) { // 40���� �� ������ Garbage Collection ����.
		Node *N = unfreeBlkList->head;
		Node *victim = N;

		// victim�� �� block�� ã�´�.
		while(N != NULL) {
			if(N->validCnt < victim->validCnt) {
				victim = N;
			}
			N = N->next;
		}

		if(victim == NULL) { // ��� block�� free�� ��� victim block�� ����.(�̷� ���� �߻����� �ʰ�����..)
			printf("[G.C.] There is no unfree block.\n");
			return;
		}

		printf("[G.C.] Victim block is (superBlock %d), and the number of valid page is %d\n", victim->blkNumber, victim->validCnt);

		if(victim->validCnt > 0) { // ��� page�� invalid�� ��� �ٷ� free block���� �Ѿ�� ��.
			for(i = 1024*(victim->blkNumber); i < 1024+1024*(victim->blkNumber); i++) { // victim block�� ������ ��ü�� �� ���鼭 ������ �Ű���.
				if(openBlk->pageCnt == 1024) { // block�� �� ���� ���� ������� �Ѿ.
					add_list_tail_node(unfreeBlkList, openBlk);
					openBlk = remove_head(freeBlkList);
				}

				if(openBlk == NULL) { // �� �̻� free block�� ���� ���.
					printf("[G.C.] There is no free block..\n");
					return;
				}
		
				// P2L[i]�� physical�� ��� ���εǾ� �ִ����� �˷���. ���� P2L[i]�� L2P�� ������ L2P[P2L[i]]�� L2P ������ ������ �ǰ�, �� ������
				// ���� ���� openBlk���� �Ű� ��. �� ��, openBlk�� ������ �ǹǷ� openBlk�� P2L���� ��� L2P ������ �Ǿ� �ִ��� ������ �˷���� ��.

				if(P2L[i] != -1) { // -1�̸� ������ �� �Ǿ� �ִ�, �� invalid��� ���̹Ƿ� �־��ָ� �� ��.
					if(L2P[P2L[i]] == i) { // �� ��쿡�� valid��. ���� ���� �ʴٸ� �ٸ� physical block���� logical�� overwrite�ߴٴ� ���̹Ƿ� invalid��� ��.
						printf("[G.C.] In L2P[%d], (superBlock %d, superPage %d) -> (superBlock %d, superPage %d)\n", P2L[i], L2P[P2L[i]]/1024, L2P[P2L[i]]%1024, openBlk->blkNumber, openBlk->pageCnt);
						L2P[P2L[i]] = 1024*openBlk->blkNumber + openBlk->pageCnt;
						P2L[1024*openBlk->blkNumber + openBlk->pageCnt] = P2L[i];
						openBlk->pageCnt++;
						openBlk->validCnt++;
					}
				}
			}
		}
	
		//mapping�� �� �Ű����Ƿ� �ٽ� free block list�� �־���.
		victim->pageCnt = 0;
		victim->validCnt = 0;
		remove_node(unfreeBlkList, victim); // unfree block list���� ����
		add_list_tail_node(freeBlkList, victim); // �ٽ� free block list�� �־���.

		if(openBlk->pageCnt == 1024) { // block�� �� ���� ���� ������� �Ѿ.
			add_list_tail_node(unfreeBlkList, openBlk);
			openBlk = remove_head(freeBlkList);
		}
	}
	printf("\n");
}

void Erase(int startAddr, int endAddr) {
	int mapsAddr = startAddr/pageSize;
	int mapeAddr = endAddr/pageSize;

	printf("Erase request to address 0x%x-0x%x\n", startAddr, endAddr);

	if(startAddr >= pageSize*1024*2048 || endAddr >= pageSize*1024*2048) {
		printf("Error: User cannot access this address\n");
		return;
	}

	if(mapsAddr == mapeAddr) { // �� �������� ���� ���.
		if(L2P[mapsAddr] != 0xFFFFFFFF) { // �����Ͱ� ���� ��츸.. 0xFFFFFFFF�� ���� X.
			find_node_with_blkNum(unfreeBlkList, L2P[mapsAddr]/1024)->validCnt--;
		}
		L2P[mapsAddr] = 0xFFFFFFFF;
		printf("[ERASE] Now L2P[%d] is invalid\n\n", mapsAddr);
	}
	else { // ���� �������� ������ �� ���
		int i;

		for(i = mapsAddr; i <= mapeAddr; i++) {
			if(L2P[i] != 0xFFFFFFFF) {
				find_node_with_blkNum(unfreeBlkList, L2P[i]/1024)->validCnt--;
			}
			L2P[i] = 0xFFFFFFFF;
			printf("[ERASE] Now L2P[%d] is invalid\n", i);
		}
		printf("\n");
	}
}

void Read(int startAddr, int chunk) { // Logical Address�� ũ�⸦ �޾� ���������� ���.
	int mapAddr = startAddr/pageSize;

	printf("Read request to address 0x%x, chunk %d\n", startAddr, chunk);

	if(chunk > 0)
		chunk--;

	if(startAddr+chunk >= pageSize*1024*2048) {
		printf("Error: User cannot access this address.\n");
		return;
	}

	if(mapAddr == (startAddr+chunk)/pageSize) { // �� �������� �д� ���.
		if(L2P[mapAddr] == 0xFFFFFFFF)
			printf("[READ] L2P[%d] is invalid\n", mapAddr);
		else
			printf("[READ] L2P[%d] (superBlock %d, superPage %d)\n", mapAddr, L2P[mapAddr]/1024, L2P[mapAddr]%1024);
	}
	else { // ���� �������� �о�� �� ���.
		int chunkNumber = (startAddr+chunk)/pageSize - mapAddr;
		
		while(chunkNumber >= 0) {
			if(L2P[mapAddr] == 0xFFFFFFFF) 
				printf("[READ] L2P[%d] is invalid\n", mapAddr);
			else
				printf("[READ] L2P[%d] (superBlock %d, superPage %d)\n", mapAddr, L2P[mapAddr]/1024, L2P[mapAddr]%1024);
			
			mapAddr++;
			chunkNumber--;
		}
	}
	printf("\n");
}

void Write(int startAddr, int chunk) {
	// �ϳ��� superblk�� 1024���� superpage�� �ִٰ� ����. page�� 16KB.
	// mapping table�� 4Byte(32bit) �� ������ 10�ڸ�(0~9)�� ������ ��ȣ, �� ���� 11�ڸ�(10~20)�� ��� ��ȣ.
	// ���� table entry�� ���� ���� page + 1024*blockNumber.
	
	int mapAddr = startAddr/pageSize; // ���̺��� �� ��° ��ġ�� ���� �� ����.

	printf("Write request to address 0x%x, chunk %d\n", startAddr, chunk);

	if(chunk > 0)
		chunk--;

	if(startAddr+chunk >= pageSize*1024*2048) {
		printf("Error: User cannot access this address.\n");
		return;
	}

	if(openBlk == NULL || openBlk->pageCnt == 1024) { // write�� ó�� �ϰų�, block�� �� �� ��� queue���� new free block�� ���� ����.
		if(openBlk != NULL && openBlk->pageCnt == 1024) // openBlk�� �� ���������Ƿ� unfreeBlock Queue�� �ִ´�.
			add_list_tail_node(unfreeBlkList, openBlk);
		
		openBlk = remove_head(freeBlkList);
		if(freeBlkList->cnt <= 20) // free block�� ���� 20�� ���ϸ� garbage collection.
			GarbageCollection();
	}

	if(openBlk == NULL) // ���⼭�� ������ remove_queue�� openBlk�� �̾Ҵµ� NULL�� ���. �� free block�� ���� ����̹Ƿ� �ٷ� ����.
		return;

	if((startAddr % pageSize) == 0) { // well aligned case.
		if(chunk < pageSize) { // chunk�� ������ ������� ���ų� ���� ��� �� �������� ��� �� �� ����.
			if(L2P[mapAddr] != 0xFFFFFFFF) { // ���� mapping�� �ִ� ��� read-back.
				printf("[WRITE] Read-back\n");
				Read(startAddr, pageSize);
				find_node_with_blkNum(unfreeBlkList, L2P[mapAddr]/1024)->validCnt--; // validCnt--;
			}
			L2P[mapAddr] = openBlk->pageCnt + 1024*openBlk->blkNumber; // ��������.
			P2L[L2P[mapAddr]] = mapAddr;
			printf("[WRITE] 0x%x: Write to L2P[%d] (superBlock %d, superPage %d)\n", startAddr, mapAddr, L2P[mapAddr]/1024, L2P[mapAddr]%1024);
			openBlk->pageCnt++;
			openBlk->validCnt++;
			printf("\n");
		}
		else { // �ƴ� ��� ���� �������� ��� ��.
			int chunkNumber = chunk/pageSize; // 0~31���� 0, 32~63���� 1, 64~95���� 2...
			int cnt = 0;
			
			while(chunkNumber >= 0) {
				if(openBlk->pageCnt == 1024) { // block�� �� ���� ���� �������.
					add_list_tail_node(unfreeBlkList, openBlk);
					openBlk = remove_head(freeBlkList);

					if(freeBlkList->cnt <= 20)
						GarbageCollection();
				}
				
				if(openBlk == NULL) { // free block�� ���� ��� return.
					return;
				}

				if(L2P[mapAddr] != 0xFFFFFFFF) { 
					if(chunkNumber == 0) { // ���� �������� �� �� �������� mapping�� �ִ� ��� read-back.
						printf("[WRITE] Read-back\n");
						Read(startAddr+cnt*pageSize, pageSize);
					}
					find_node_with_blkNum(unfreeBlkList, L2P[mapAddr]/1024)->validCnt--; // invalid�� �þ�Ƿ� --����.
				}

				L2P[mapAddr] = openBlk->pageCnt + 1024*openBlk->blkNumber;
				P2L[L2P[mapAddr]] = mapAddr;
				printf("[WRITE] 0x%x: Write to L2P[%d] (superBlock %d, superPage %d)\n", startAddr+cnt*pageSize, mapAddr, L2P[mapAddr]/1024, L2P[mapAddr]%1024);
				openBlk->pageCnt++;
				openBlk->validCnt++;
				mapAddr++;
				chunkNumber--;
				cnt++;
				printf("\n");
			}
		}
	}
	else { // misaligned case�� ��� read-back�� �� ���� ���� mapping�� �ؾ� ��.
		printf("[WRITE] Read-back\n");

		Read(mapAddr*pageSize, pageSize); // read-back
		if(L2P[mapAddr] != 0xFFFFFFFF) 
			find_node_with_blkNum(unfreeBlkList, L2P[mapAddr]/1024)->validCnt--;

		if(mapAddr == (startAddr+chunk)/pageSize) { // �� �������� �� ���, �׳� read-back �� write.
			L2P[mapAddr] = openBlk->pageCnt + 1024*openBlk->blkNumber;
			P2L[L2P[mapAddr]] = mapAddr;
			
			printf("[WRITE] 0x%x: Write to L2P[%d] (superBlock %d, superPage %d)\n", mapAddr*pageSize, mapAddr, L2P[mapAddr]/1024, L2P[mapAddr]%1024);
			openBlk->pageCnt++;
			openBlk->validCnt++;
			printf("\n");
		}
		else { // ���� �������� �� ���, ó�� �������� read-back �� �ʿ��� ������ ����ŭ write.
			int chunkNumber = (startAddr+chunk)/pageSize - mapAddr;

			while(chunkNumber >= 0) {
				if(openBlk->pageCnt == 1024) { // block�� �� ���� ���� �������.
					add_list_tail_node(unfreeBlkList, openBlk);
					openBlk = remove_head(freeBlkList);

					if(freeBlkList->cnt <= 20)
						GarbageCollection();
				}
				
				if(openBlk == NULL) { // free block�� ���� ��� return.
					return;
				}

				if(L2P[mapAddr] != 0xFFFFFFFF) { // ���� �������� �� �� �������� ���� mapping�� �ִ� ��� read-back.
					if(chunkNumber == 0) {
						printf("[WRITE] Read-back\n");
						Read(mapAddr*pageSize, pageSize);
					}
					find_node_with_blkNum(unfreeBlkList, L2P[mapAddr]/1024)->validCnt--;
				}

				L2P[mapAddr] = openBlk->pageCnt + 1024*openBlk->blkNumber;
				P2L[L2P[mapAddr]] = mapAddr;
				printf("[WRITE] 0x%x: Write to L2P[%d] (superBlock %d, superPage %d)\n", mapAddr*pageSize, mapAddr, L2P[mapAddr]/1024, L2P[mapAddr]%1024);
				openBlk->pageCnt++;
				openBlk->validCnt++;
				mapAddr++;
				chunkNumber--;
				printf("\n");
			}
		}
	}
}

int main() {
	int i;
	Node *tmp;
	freeBlkList = create_list();
	unfreeBlkList = create_list();

	for( i = 0; i < 2098; i++ ) { //superblk�� ũ��� 16MB�̰� flash�� ũ��� 32GB�̹Ƿ� block�� �� 2048�� �ʿ���. overflow�� ���� ���� ����� overflow division block�� 50�� ����.
		add_list_tail(freeBlkList, i);
	}

	for(i = 0; i < 1024*2098; i++) { 
		L2P[i] = 0xFFFFFFFF; // ó���� mapping table�� �ʱ�ȭ. ���� 0xFFFFFFFF�� ��� ���� ���� ���� ��, �ٸ� ���� ��� �̹� �� ������ �� �� ����.
		P2L[i] = -1; // P2L�� ���� ������ �Ǿ����� L2P �迭�� ��� ���εǾ� �ִ��� ���� �� �ְ�, �� �Ǿ� ������ -1.(�̰͵� 0xFFFFFFFF��.)
	}
	
	// test ����. ������ 0~2047������ block�� ���� ����.
	
	Write(0, pageSize*1024*2048);
	Write(0, pageSize*1024*30);
	Write(pageSize*1024*77, pageSize*1024*20);
	Write(pageSize*1024*119, pageSize*1024*120);

	tmp = freeBlkList -> head;
	while(tmp != NULL) {
		printf("block %d is free\n", tmp->blkNumber);
		tmp = tmp->next;
	}

	return 0;
}