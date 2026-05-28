#include "functions.h"

#include <iostream>
#include <stdio.h>

#define _CRT_SECURE_NO_WARNINGS

CFunctions func;

static RFC_FUNCTION_HANDLE hBAPI_user_get_detail;
static RFC_STRUCTURE_HANDLE h_struct = nullptr;
static RFC_FUNCTION_HANDLE h_func = nullptr;
static RFC_TABLE_HANDLE h_table = nullptr;
static RFC_TYPE_DESC_HANDLE h_type = nullptr;
static RFC_FIELD_DESC h_field;
static DATA_CONTAINER_HANDLE h_cont;

static RFC_RC SAP_API mat_pi_out(const RFC_CONNECTION_HANDLE rfc_handle, const RFC_FUNCTION_HANDLE func_handle, RFC_ERROR_INFO* error_info) {
	
	RFC_RC rc = RFC_OK;
	unsigned length;
	unsigned int tables, field_count;
	
	RFC_ATTRIBUTES attributes;
	RFC_ERROR_INFO error;
	RFC_STRUCTURE_HANDLE address, company, zscpp_mat_data2;
	SAP_UC requtext[256], resptext[256], echotext[256];

	rc = RfcGetStructure(func_handle, cU("IS_INPUT"), &h_struct, &error); /* zscpp_mat */
	rc = RfcGetTable(h_struct, cU("MATERIALDATA"), &h_table, &error);	  /* zscpp_mat_data */
	RfcGetRowCount(h_table, &tables, &error);
	zscpp_mat_data2 = RfcGetCurrentRow(h_table, &error);				  /* zscpp_mat_data2 */
	

	h_type = RfcDescribeType(zscpp_mat_data2, error_info);
	rc = RfcGetFieldCount(h_type, &field_count, error_info);

	for (int i = 0; i < field_count; i++) {
		rc = RfcGetFieldDescByIndex(h_type, i, &h_field, error_info);

			const int x = h_field.nucLength;

			SAP_UC buf_matnr[18 + 1];			
			rc = RfcGetChars(zscpp_mat_data2, cU("MATERIALNUMBER"), buf_matnr, sizeof(buf_matnr), &error);

			printfU(cU("MATNR : %s\n"), buf_matnr);
		}

	RfcGetString(func_handle, cU("REQUTEXT"), requtext, 256, &length, nullptr);
	printfU(cU("\nReceived request for ZRFC_cpp_MAT_OUT\n"));
	printfU(cU("REQUTEXT = %s\n"), requtext);

	RfcGetConnectionAttributes(rfc_handle, &attributes, nullptr);
	printfU(cU("User is %s. Let's see what we can find out about him/her.\n"), attributes.user);

	RfcSetString(hBAPI_user_get_detail, cU("USERNAME"), attributes.user, strlenU(attributes.user), nullptr);
	RfcGetStructure(hBAPI_user_get_detail, cU("ADDRESS"), &address, nullptr);
	RfcSetString(address, cU("FULLNAME"), cU(" "), 1, nullptr);
	RfcGetStructure(hBAPI_user_get_detail, cU("COMPANY"), &company, nullptr);
	RfcSetString(company, cU("COMPANY"), cU(" "), 1, nullptr);

	rc = RfcInvoke(rfc_handle, hBAPI_user_get_detail, error_info);
	if (rc != RFC_OK) {
		memcpy(echotext, cU("Calling BAPI_USER_GET_DETAIL failed: "), 50);
		memcpy(resptext, error_info->message, strlenU(error_info->message));
		error_info->message[0] = 0;
		error_info->code = RFC_OK;
	}
	else {
		memcpy(echotext, cU("whoami "), 12);
		RfcGetStructure(hBAPI_user_get_detail, cU("ADDRESS"), &address, nullptr);
		RfcGetString(address, cU("FULLNAME"), echotext + 12, 244, &length, nullptr);
		memcpy(resptext, cU("company "), 16);
		RfcGetStructure(hBAPI_user_get_detail, cU("COMPANY"), &company, nullptr);
		RfcGetString(company, cU("COMPANY"), resptext + 16, 241, &length, nullptr);
	}
	RfcSetString(func_handle, cU("RESPTEXT"), resptext, strlenU(resptext), nullptr);
	RfcSetString(func_handle, cU("ECHOTEXT"), echotext, strlenU(echotext), nullptr);

	printfU(cU("Do you want to stop listening after this request? [y/n] "));

	rsize_t RSIZE_MAX = 1024;
	fgetsU(requtext, RSIZE_MAX, stdin);
	/* getsU(requtext); */

	if (*requtext == cU('y')) {
		func.listening = 0;
	}

	return RFC_OK;
}

int main(int argc, SAP_UC** argv) {
	func.listening = 1;
	RFC_RC rc = RFC_OK;
	RFC_ERROR_INFO errorInfo;
	RFC_CONNECTION_PARAMETER gatewayParams[1];
	RFC_CONNECTION_HANDLE server_handle = nullptr;
	RFC_CONNECTION_HANDLE connection;
	RFC_FUNCTION_DESC_HANDLE hdesc_mat, hdesc_serv;

	RFC_ATTRIBUTES attributes;

	gatewayParams[0].name = cU("dest");
	gatewayParams[0].value = cU("DEV"); /* system из .ini1 */

	/* Получаем метаданные из функции */
	connection = RfcOpenConnection(gatewayParams, 1, &errorInfo);
	if (connection == nullptr) {
		printfU(cU("Error during logon: %s\n"), errorInfo.message);
		printfU(cU("Please check that the sapnwrfc.ini file is in the current\nworking directory and the logon parameters are ok.\n"));
		return 1;
	}

	rc = RfcGetConnectionAttributes(connection, &attributes, nullptr);
	if (rc == RFC_OK)
		printfU(cU("Successfully logged on to destination (System-ID %s)\n"), attributes.sysId);

	hdesc_mat = RfcGetFunctionDesc(connection, cU("ZRFC_CPP_MAT_OUT"), &errorInfo);
	hdesc_serv = RfcGetFunctionDesc(connection, cU("ZRFC_CPP_SERV_OUT"), &errorInfo);

	RfcCloseConnection(connection, nullptr);
	if (hdesc_mat == nullptr || hdesc_serv == nullptr) {
		printfU(cU("Error getting metadata: %s\n"), errorInfo.message);
		return 1;
	}
	// мета

	rc = RfcInstallServerFunction(nullptr, hdesc_mat, mat_pi_out, &errorInfo);
	if (rc != RFC_OK) {
		printfU(cU("Unable to install RequestHandler: %s: %s\n"), RfcGetRcAsString(errorInfo.code), errorInfo.message);
		return 1;
	}

	hBAPI_user_get_detail = RfcCreateFunction(hdesc_serv, &errorInfo);
	if (hBAPI_user_get_detail == nullptr) {
		printfU(cU("Error creating data container: %s\n"), errorInfo.message);
		return 1;
	}

	rc = RfcInstallGenericServerFunction(&func.generic_request_handler, &func.repository_lookup, &errorInfo);
	if (rc != RFC_OK) {
		printfU(cU("Unable to install RequestHandler: %s: %s\n"), RfcGetRcAsString(errorInfo.code), errorInfo.message);
		return 1;
	}

	server_handle = RfcRegisterServer(gatewayParams, 1, &errorInfo);
	if (server_handle == nullptr) printfU(cU("Unable to register at %s: %s: %s\n"), gatewayParams[0].value,
		RfcGetRcAsString(errorInfo.code), errorInfo.message);
	else printfU(cU("Successfully registered at destination %s\n"), gatewayParams[0].value);

	printfU(cU("\nWaiting for request...\n"));
	while (server_handle != nullptr) {
		gatewayParams->value = gatewayParams[0].value;
		if (server_handle != nullptr) func.listen(&server_handle, gatewayParams);

		if (!func.listening) break;
	}

	if (server_handle != nullptr) RfcCloseConnection(server_handle, nullptr);

	return 0;
}

