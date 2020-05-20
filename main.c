//doar 2 butoane si un LED
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

#define sbi(var, mask)   ((var) |= (uint8_t)(1 << mask))
#define cbi(var, mask)   ((var) &= (uint8_t)~(1 << mask))

//Define functions
//======================
void ioinit(void);      //Initializes IO
void delay_ms(uint16_t x); //General purpose delay
void delay_us(uint8_t x);

uint8_t data_array[4], config[100];

#define TX_CE	1 //Output
#define TX_CSN	2 //Output

//#define RF_DELAY	5
#define RF_DELAY	55

#define BUTTON0	0
#define BUTTON1	1
#define LED	2

#define soft 
//hardware trimite doar primul pachet
//detecteaza butoanele dar nu mai trimite

#if defined(soft)
//software

#define TX_SCK	4 //Output
#define TX_MISO 6 //Input
#define TX_MOSI	5 //Output

uint8_t tx_spi_byte(uint8_t outgoing)
{
	uint8_t i, incoming;
	incoming = 0;

	//Send outgoing byte
	for(i = 0 ; i < 8 ; i++)
	{
		if(outgoing & 0b10000000)
		sbi(PORTA, TX_MOSI);
		else
		cbi(PORTA, TX_MOSI);
		
		sbi(PORTA, TX_SCK); //TX_SCK = 1;
		delay_us(RF_DELAY);

		//MISO bit is valid after clock goes going high
		incoming <<= 1;
		if( PINA & (1<<TX_MISO) ) incoming |= 0x01;

		cbi(PORTA, TX_SCK); //TX_SCK = 0;
		delay_us(RF_DELAY);
		
		outgoing <<= 1;
	}

	return(incoming);
}

#else

//hardware e fix invers
//cu 38byte mai putin
//nu transmite starea butoanelor
uint8_t tx_spi_byte(uint8_t outgoing)
{
	//Load data
	USIDR = outgoing;
	
	//Clear the USI counter overflow flag
	USISR = (1<< USIOIF);

	do
	{
		USICR = (1<< USIWM0) | (1<< USICLK) | (1<< USICS1) | (1<< USITC);
	} while((USISR & (1<< USIOIF)) == 0);
	
	return USIDR;
}

#endif

//EEPROM 
uint8_t EEPROM_read( uint8_t address)
{
	/* Wait for completion of previous write */
	while(EECR & (1<<EEPE))
	;
	/* Set up address register */
	EEAR = address;
	/* Start eeprom read by writing EERE */
	EECR |= (1<<EERE);
	/* Return data from data register */
	return EEDR;
}


void EEPROM_read2(uint8_t addr_start, uint8_t addr_stop) {
	uint8_t i=0;
	while(EECR & (1<<EEPE))
	;
	while(addr_start+i<=addr_stop) {
		EEAR=addr_start+i;
		EECR |= (1<<EERE);
		config[i]=EEDR;
		i++;
	}
}


//2.4G Configuration - Transmitter
uint8_t configure_transmitter(void);
//Sends command to nRF
uint8_t tx_send_byte(uint8_t cmd);
//Basic SPI to nRF
uint8_t tx_send_command(uint8_t cmd, uint8_t data);
//Sends the 4 bytes of payload
void tx_send_payload(uint8_t cmd);
//This sends out the data stored in the data_array
void transmit_data(void);
//Basic SPI to nRF
uint8_t tx_spi_byte(uint8_t outgoing);

//TX Functions
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

//This sends out the data stored in the data_array
//data_array must be setup before calling this function
void transmit_data(void)
{
	tx_send_command(0x27, 0x7E); //Clear any interrupts
	
	tx_send_command(0x20, 0x7A); //Power up and be a transmitter

	tx_send_byte(0xE1); //Clear TX Fifo
	
	tx_send_payload(0xA0); //Clock in 4 byte payload of data_array

    sbi(PORTB, TX_CE); //Pulse CE to start transmission
    delay_ms(3);
    cbi(PORTB, TX_CE);
}

//2.4G Configuration - Transmitter
//This sets up one RF-24G for shockburst transmission
uint8_t configure_transmitter(void)
{
	uint8_t i=0;
	
	cbi(PORTB, TX_CE); //Go into standby mode

	while(i<7) {
		tx_send_command(0x20+i, config[i]);
		i++;
	}
	tx_send_command(0x3C, config[i]);
	i++;
	tx_send_command(0x3D, config[i]);
	i=0;
	
	//tx_send_command(0x20, config[i]); //CRC enabled, be a transmitter 0x78
	////pe crc 2byte nu merge
	//tx_send_command(0x21, config[i]); //Disable auto acknowledge on all pipes 0x00
	//tx_send_command(0x22, config[i]); //activare adrese rx 0x00
	//tx_send_command(0x23, config[i]); //Set address width to 5bytes (default, not really needed) 0x03
	//tx_send_command(0x24, config[i]); //Disable auto-retransmit 0x00
	//tx_send_command(0x25, config[i]); //RF Channel 2 (default, not really needed) 0x02
	////tx_send_command(0x26, 0x07); //Air data rate 1Mbit, 0dBm, Setup LNA
	//tx_send_command(0x26, 0b00100000); //Air data rate 1Mbit, -18dBm, Setup LNA 0x01

	data_array[0] = 0xE7;
	data_array[1] = 0xE7;
	data_array[2] = 0xE7;
	data_array[3] = 0xE7;
	tx_send_payload(0x30); //Set TX address
	
	tx_send_command(0x20, 0x7A); //Power up, be a transmitter

	return(tx_send_byte(0xFF));
}

//Sends the 4 bytes of payload
void tx_send_payload(uint8_t cmd)
{
	uint8_t i;

	cbi(PORTB, TX_CSN); //Select chip
	tx_spi_byte(cmd);
	
	for(i = 0 ; i < 4 ; i++)
		tx_spi_byte(data_array[i]);

	sbi(PORTB, TX_CSN); //Deselect chip
}

//Sends command to nRF
uint8_t tx_send_command(uint8_t cmd, uint8_t data)
{
	uint8_t status;

	cbi(PORTB, TX_CSN); //Select chip
	tx_spi_byte(cmd);
	status = tx_spi_byte(data);
	sbi(PORTB, TX_CSN); //Deselect chip

	return(status);
}

//Sends one byte to nRF
uint8_t tx_send_byte(uint8_t cmd)
{
	uint8_t status;
	
	cbi(PORTB, TX_CSN); //Select chip
	status = tx_spi_byte(cmd);
	sbi(PORTB, TX_CSN); //Deselect chip
	
	return(status);
}

//======================


ISR(PCINT0_vect)
{
	//This vector is only here to wake unit up from sleep mode
}

int main (void)
{
	//1 = Output, 0 = Input
	DDRA = 0xFF & ~(1<<6 | 1<<BUTTON0 | 1<<BUTTON1 );
	DDRB = 0b00000110; //(CE on PB1) (CS on PB2)

	//Enable pull-up resistors (page 74)
	PORTA = 0b01000011; //Pulling up a pin that is grounded will cause 90uA current leak

	cbi(PORTB, TX_CE); //Stand by mode
	
	//Init Timer0 for delay_us
	TCCR0B = (1<<CS00); //Set Prescaler to No Prescaling (assume we are running at internal 1MHz). CS00=1
	
	sbi(PORTA, LED);
	delay_ms(200);
	cbi(PORTA, LED);
	delay_ms(200);
	sbi(PORTA, LED);
	delay_ms(200);
	cbi(PORTA, LED);
	
	EEPROM_read2(0x00, 0x08);

	configure_transmitter();

	GIFR = (1<<PCIF0); //Enable the Pin Change interrupts to monitor button presses
	GIMSK = (1<<PCIE0); //Enable Pin Change Interrupt Request
	PCMSK0 = (1<<BUTTON0)|(1<<BUTTON1);
	MCUCR = (1<<SM1)|(1<<SE); //Setup Power-down mode and enable sleep
	
	sei(); //Enable interrupts
	
	transmit_data(); //Send one packet when we turn on
	while(1)
	{
		if( (PINA & 0x03) != 0x03 ) //verifica PA0, PA1
		{
			data_array[0] = PINA & 0x03; //salveaza PA0-3
			
			//data_array[0] |= (PINA & 0x80) >> 3; //salveaza PA7
			
			sbi(PORTA, LED);
			delay_ms(200);
			cbi(PORTA, LED);

			transmit_data();
		}
		
		tx_send_command(0x20, 0x00); //Power down RF

		cbi(PORTB, TX_CE); //Go into standby mode
		sbi(PORTB, TX_CSN); //Deselect chip
		
		ACSR = (1<<ACD); //Turn off Analog Comparator - this removes about 1uA
		PRR = 0x0F; //Reduce all power right before sleep
		asm volatile ("sleep");
		//Sleep until a button wakes us up on interrupt
	}
	
    return(0);
}


//General short delays
void delay_ms(uint16_t x)
{
	for (; x > 0 ; x--)
	{
		delay_us(250);
		delay_us(250);
		delay_us(250);
		delay_us(250);
	}
}

//General short delays
void delay_us(uint8_t x)
{
	TIFR0 = 0x01; //Clear any interrupt flags on Timer2
	
    TCNT0 = 256 - x; //256 - 125 = 131 : Preload timer 2 for x clicks. Should be 1us per click

	while( (TIFR0 & (1<<TOV0)) == 0);
}
