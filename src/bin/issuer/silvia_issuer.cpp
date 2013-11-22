/* $Id$ */

/*
 * Copyright (c) 2013 Roland van Rijswijk-Deij
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*****************************************************************************
 silvia_issuer.cpp

 Command-line issuer utility
 *****************************************************************************/

#include "config.h"
#include "silvia_parameters.h"
#include "silvia_irma_issuer.h"
#ifdef WITH_PCSC
#include "silvia_pcsc_card.h"
#endif // WITH_PCSC
#ifdef WITH_NFC
#include "silvia_nfc_card.h"
#endif // WITH_NFC
#include "silvia_card_channel.h"
#include "silvia_irma_xmlreader.h"
#include "silvia_idemix_xmlreader.h"
#include "silvia_types.h"
#include <string>
#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>

void signal_handler(int signal)
{
	// Exit on any signal we receive and handle
	fprintf(stderr, "\nCaught signal, exiting...\n");
	
	exit(0);
}

void set_parameters()
{
	////////////////////////////////////////////////////////////////////
	// Set the system parameters in the IRMA library; this function must
	// be updated if we ever change the parameters for IRMA cards!!!
	////////////////////////////////////////////////////////////////////
	
	silvia_system_parameters::i()->set_l_n(1024);
	silvia_system_parameters::i()->set_l_m(256);
	silvia_system_parameters::i()->set_l_statzk(80);
	silvia_system_parameters::i()->set_l_H(256);
	silvia_system_parameters::i()->set_l_v(1700);
	silvia_system_parameters::i()->set_l_e(597);
	silvia_system_parameters::i()->set_l_e_prime(120);
	silvia_system_parameters::i()->set_hash_type("sha256");
}

void version(void)
{
	printf("The Simple Library for Verifying and Issuing Attributes (silvia)\n");
	printf("\n");
	printf("Command-line issuing utility for IRMA cards %s\n", VERSION);
	printf("\n");
	printf("Copyright (c) 2013 Roland van Rijswijk-Deij\n\n");
	printf("Use, modification and redistribution of this software is subject to the terms\n");
	printf("of the license agreement. This software is licensed under a 2-clause BSD-style\n");
	printf("license a copy of which is included as the file LICENSE in the distribution.\n");
}

void usage(void)
{
	printf("Silvia command-line IRMA issuer %s\n\n", VERSION);
	printf("Usage:\n");
	printf("\tsilvia_issuer -I <issue-spec> -k <issuer-pubkey> -s <issuer-privkey>");
#if defined(WITH_PCSC) && defined(WITH_NFC)
	printf(" [-P] [-N]");
#endif // WITH_PCSC && WITH_NFC
	printf("\n");
	printf("\tsilvia_issuer -h\n");
	printf("\tsilvia_issuer -v\n");
	printf("\n");
	printf("\t-I <issue-spec>     Read issue specification from <issue-spec>\n");
	printf("\t-k <issuer-pubkey>  Read issuer public key from <issuer-pubkey>\n");
	printf("\t-s <issuer-privkey> Read issuer private key from <issuer-privkey>\n");
#if defined(WITH_PCSC) && defined(WITH_NFC)
	printf("\t-P                 Use PC/SC for card communication (default)\n");
	printf("\t-N                 Use NFC for card communication\n");
#endif // WITH_PCSC && WITH_NFC
	printf("\n");
	printf("\t-h                 Print this help message\n");
	printf("\n");
	printf("\t-v                 Print the version number\n");
}

std::string get_pin()
{
	printf("\n");
	printf("=================================================\n");
	printf("            PIN VERIFICATION REQUIRED            \n");
	printf("=================================================\n");
	printf("\n");
	
	std::string PIN;
	
	do
	{ 
		PIN = getpass("Please enter your PIN: ");
		
		if (PIN.size() > 8)
		{
			printf("PIN too long; 8 characters or less expected!\n");
		}
		else if (PIN.empty())
		{
			printf("You must enter a PIN!\n");
		}
	}
	while (PIN.empty() || (PIN.size() > 8));
	
	printf("\n");
	
	return PIN;
}

bytestring bs2str(const bytestring& in)
{
	bytestring out = in;
	
	// Strip leading 00's
	while ((out.size() > 0) && (out[0] == 0x00))
	{
		out = out.substr(1);
	}
	
	// Append null-termination
	out += 0x00;
	
	return out;
}

bool communicate_with_card(silvia_card_channel* card, std::vector<bytestring>& commands, std::vector<bytestring>& results)
{
	//printf("Communicating with the card... "); fflush(stdout);
		
	bool comm_ok = true;
	size_t cmd_ctr = 0;
	
	for (std::vector<bytestring>::iterator i = commands.begin(); (i != commands.end()) && comm_ok; i++)
	{	
		bytestring result;
		
		printf("--> %s\n", i->hex_str().c_str());
		
		if (!card->transmit(*i, result))
		{
			comm_ok = false;
			break;
		}
		
		printf("<-- %s\n", result.hex_str().c_str());
		
		cmd_ctr++;
		
		if (result.substr(result.size() - 2) != "9000")
		{
			printf("(0x%s) ", result.substr(result.size() - 2).hex_str().c_str());
			comm_ok = false;
			break;
		}
		
		results.push_back(result);
	}
	
	if (comm_ok)
	{
		printf("OK\n");
	}
	else
	{
		printf("FAILED!\n");
	}
	
	return comm_ok;
}

void do_issue(int channel_type, std::string issue_spec, std::string issuer_pubkey, std::string issuer_privkey)
{
	silvia_card_channel* card = NULL;
	
	printf("Silvia command-line IRMA issuer %s\n\n", VERSION);
		
	// Read configuration files
	silvia_issue_specification* ispec = silvia_irma_xmlreader::i()->read_issue_spec(issue_spec);
	
	if (ispec == NULL)
	{
		fprintf(stderr, "Failed to read issue specification\n");
		
		return;
	}
	
	silvia_pub_key* pubkey = silvia_idemix_xmlreader::i()->read_idemix_pubkey(issuer_pubkey);
	
	if (pubkey == NULL)
	{
		fprintf(stderr, "Failed to read issuer public key\n");
		
		delete ispec;
		
		return;
	}
	
	silvia_priv_key* privkey = silvia_idemix_xmlreader::i()->read_idemix_privkey(issuer_privkey);
	
	if (privkey == NULL)
	{
		fprintf(stderr, "Failed to read issuer private key\n");
		
		delete pubkey;
		delete ispec;
		
		return;
	}
	
	// Create issuer object
	silvia_irma_issuer issuer(pubkey, privkey, ispec);
	
	// Wait for card
	printf("\n********************************************************************************\n");
	printf("Waiting for card");
	
#ifdef WITH_PCSC
	if (channel_type == SILVIA_CHANNEL_PCSC)
	{
		printf(" (PCSC) ..."); fflush(stdout);
		
		silvia_pcsc_card* pcsc_card = NULL;
		
		if (!silvia_pcsc_card_monitor::i()->wait_for_card(&pcsc_card))
		{
			printf("FAILED, exiting\n");
			
			exit(-1);
		}
		
		card = pcsc_card;
	}
#endif // WITH_PCSC
#ifdef WITH_NFC
	if (channel_type == SILVIA_CHANNEL_NFC)
	{
		printf(" (NFC) ..."); fflush(stdout);
		
		silvia_nfc_card* nfc_card = NULL;
		
		if (!silvia_nfc_card_monitor::i()->wait_for_card(&nfc_card))
		{
			printf("FAILED, exiting\n");
			
			exit(-1);
		}
		
		card = nfc_card;
	}
#endif // WITH_NFC
		
	printf("OK\n");
	
	// Ask the user to enter their PIN
	std::string PIN = get_pin();
	
	// First, perform application selection
	std::vector<bytestring> commands;
	std::vector<bytestring> results;
	
	commands = issuer.get_select_commands(PIN);
	
	if (communicate_with_card(card, commands, results))
	{
		if (issuer.submit_select_data(results))
		{
			// Perform the first round of issuance
			commands.clear();
			results.clear();
			
			commands = issuer.get_issue_commands_round_1();
			
			if (communicate_with_card(card, commands, results))
			{
				// Verify the return data of the first round
				if (issuer.submit_issue_results_round_1(results))
				{
					commands.clear();
					results.clear();
					
					commands = issuer.get_issue_commands_round_2();
					
					if (communicate_with_card(card, commands, results))
					{
						if (issuer.submit_issue_results_round_2(results))
						{
							printf("Credential issued successfully\n");
						}
						else
						{
							printf("Round 2 of issuance FAILED!\n");
							
							issuer.abort();
						}
					}
					else
					{
						printf("Failed to communicate with the card, was it removed prematurely?\n");
				
						issuer.abort();
					}
				}
				else
				{
					printf("Round 1 of issuance FAILED!\n");
					
					issuer.abort();
				}
			}
			else
			{
				printf("Failed to communicate with the card, was it removed prematurely?\n");
				
				issuer.abort();
			}
		}
		else
		{
			printf("Failed to select IRMA application!\n");
			
			issuer.abort();
		}
	}
	else
	{
		printf("Failed to communicate with the card, was it removed prematurely?\n");
		
		issuer.abort();
	}
	
	printf("Waiting for card to be removed... "); fflush(stdout);
	
	while (card->status())
	{
		usleep(10000);
	}
	
	printf("OK\n");
	
	printf("********************************************************************************\n");
	
	//delete card;
	
	//delete ispec;
	//delete pubkey;
	//delete privkey;
}

int main(int argc, char* argv[])
{
	// Set library parameters
	set_parameters();
	
	// Program parameters
	std::string issue_spec;
	std::string issuer_pubkey;
	std::string issuer_privkey;
	int c = 0;
#if defined(WITH_PCSC) && defined(WITH_NFC)
	int channel_type = SILVIA_CHANNEL_PCSC;
#elif defined(WITH_PCSC)
	int channel_type = SILVIA_CHANNEL_PCSC;
#elif defined(WITH_NFC)
	int channel_type = SILVIA_CHANNEL_NFC;
#endif
	
#if defined(WITH_PCSC) && defined(WITH_NFC)
	while ((c = getopt(argc, argv, "I:k:s:hvPN")) != -1)
#else
	while ((c = getopt(argc, argv, "I:k:s:hv")) != -1)
#endif
	{
		switch (c)
		{
		case 'h':
			usage();
			return 0;
		case 'v':
			version();
			return 0;
		case 'I':
			issue_spec = std::string(optarg);
			break;
		case 'k':
			issuer_pubkey = std::string(optarg);
			break;
		case 's':
			issuer_privkey = std::string(optarg);
			break;
#if defined(WITH_PCSC) && defined(WITH_NFC)
		case 'P':
			channel_type = SILVIA_CHANNEL_PCSC;
			break;
		case 'N':
			channel_type = SILVIA_CHANNEL_NFC;
			break;
#endif
		}
	}
	
	if (issue_spec.empty())
	{
		fprintf(stderr, "No issue specification file specified!\n");
		
		return -1;
	}
	
	if (issuer_pubkey.empty())
	{
		fprintf(stderr, "No issuer public key file specified!\n");
		
		return -1;
	}
	
	if (issuer_privkey.empty())
	{
		fprintf(stderr, "No issuer private key file specified!\n");
		
		return -1;
	}
	
#ifdef WITH_NFC
	if (channel_type == SILVIA_CHANNEL_NFC)
	{
		// Handle signals when using NFC; this prevents the NFC reader
		// from going into an undefined state when the user aborts the
		// program by pressing Ctrl+C
		signal(SIGQUIT, signal_handler);
		signal(SIGTERM, signal_handler);
		signal(SIGINT, signal_handler);
		signal(SIGABRT, signal_handler);
	}
#endif
	
	do_issue(channel_type, issue_spec, issuer_pubkey, issuer_privkey);
	
	return 0;
}
