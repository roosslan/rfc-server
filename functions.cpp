#include <stdlib.h>
#include <stdio.h>

#include "functions.h"

#include <cstdint>

#define BUF_SIZE 1024
static SAP_UC buffer[BUF_SIZE];

int CFunctions::listening = 1;

RFC_RC SAP_API CFunctions::generic_request_handler(const RFC_CONNECTION_HANDLE rfc_handle, const RFC_FUNCTION_HANDLE func_handle, RFC_ERROR_INFO* error_info) {
	RFC_ATTRIBUTES attributes;
	RFC_ABAP_NAME func_name;

	printfU(cU("\n---------------------------------------\n"));
	RfcGetConnectionAttributes(rfc_handle, &attributes, nullptr);
	const RFC_FUNCTION_DESC_HANDLE metadata = RfcDescribeFunction(func_handle, nullptr);
	RfcGetFunctionName(metadata, func_name, nullptr);

	RFC_RC rc = check_authorization(func_name, attributes);
	if (rc != RFC_OK) {
		error_info->code = RFC_EXTERNAL_FAILURE;
		memcpy(error_info->message, cU("Access denied!"), 25);
		rc = RFC_EXTERNAL_FAILURE;
		printfU(cU("\nStop listening after this request? [y/n] "));
		if (on_agree_click())
			listening = 0;
		return rc;
	}

	printfU(cU("\n"));
	print_imports(metadata, func_handle);

	printfU(cU("\n"));
	error_throwing(metadata, error_info);
	if (error_info->code != RFC_OK) {
		rc = error_info->code;
		printfU(cU("\nStop listening after this request? [y/n] "));
		if (on_agree_click())
			listening = 0;
		return rc;
	}

	printfU(cU("\n"));
	fill_exports(metadata, func_handle);

	printfU(cU("\nStop listening after this request? [y/n] "));
	if (on_agree_click())
		listening = 0;
	return rc;
}

void CFunctions::listen(RFC_CONNECTION_HANDLE* pserver_handle, const RFC_CONNECTION_PARAMETER* params) {
	RFC_ERROR_INFO error_info;
	int refresh = 0;

	const RFC_RC rc = RfcListenAndDispatch(*pserver_handle, 2, &error_info);
	switch (rc) {
	case RFC_OK:
	case RFC_RETRY:
		break;

	case RFC_ABAP_EXCEPTION:
		printfU(cU("ABAP_EXCEPTION in implementing function: %s\n"), error_info.key);
		break;

	case RFC_NOT_FOUND:
		printfU(cU("Unknown function module: %s\n"), error_info.message);
		refresh = 1;
		break;

	case RFC_EXTERNAL_FAILURE:
		printfU(cU("SYSTEM_FAILURE has been sent to backend: %s\n"), error_info.message);
		refresh = 1;
		break;

	case RFC_ABAP_MESSAGE:
		printfU(cU("ABAP Message has been sent to backend: %s %s %s\n"), error_info.abapMsgType,
			error_info.abapMsgClass, error_info.abapMsgNumber);
		printfU(cU("Variables: V1=%s V2=%s V3=%s V4=%s\n"), error_info.abapMsgV1,
			error_info.abapMsgV2, error_info.abapMsgV3, error_info.abapMsgV4);
		refresh = 1;
		break;

	case RFC_COMMUNICATION_FAILURE:
	case RFC_CLOSED:
		printfU(cU("Connection broke down during transmission of return values: %s\n"), error_info.message);
		refresh = 1;
		break;

	default: ;
	}

	if (refresh) {
		printfU(cU("Trying to reconnect...\n"));
		*pserver_handle = RfcRegisterServer(params, 1, &error_info);
		if (*pserver_handle == nullptr) {
			printfU(cU("Unable to reconnect to %s: %s: %s\n"), params->value,
				RfcGetRcAsString(error_info.code), error_info.message);
			printfU(cU("Stopping to listen at %s"), params->value);
		}
	}

	if (rc != RFC_RETRY && *pserver_handle != nullptr && listening) printfU(cU("\nWaiting for request...\n"));
}

void CFunctions::error_throwing(const RFC_FUNCTION_DESC_HANDLE metadata, RFC_ERROR_INFO* error_info) {

	unsigned i, param_count = 0;
	int index = 2, choice = 0;

	RFC_EXCEPTION_DESC excep_desc;
	SAP_UC value[203];

	error_info->code = RFC_OK;

	printf(("For this function we can throw the following errors:\n"));
	printfU(cU("1. SYSTEM_FAILURE\n"));
	printfU(cU("2. ABAP Message\n"));

	RfcGetExceptionCount(metadata, &param_count, nullptr);
	for (i = 0; i < param_count; i++) {
		RfcGetExceptionDescByIndex(metadata, i, &excep_desc, nullptr);
		printfU(cU("%d. %s: %s\n"), ++index, excep_desc.key, excep_desc.message);
	}

	printfU(cU("Please enter a value between 1 and %d or \"n\" for not throwing an exception: "), index);
	read_value(value, 203);

	if (strncmpU(value, cU("n"), 1) == 0)
		return;

	choice = atoiU(value);
	if (choice < 1 || choice > index)
		return;

	switch (choice) {
	case 1:
		printfU(cU("Please enter a value for the message (200): "));
		read_value(value, 203);  /* In the worst case we need one extra for each of \r\n\0 */
		i = strlenU(value);

		memcpy(error_info->message, value, i < 200 ? i : 200);

		error_info->code = RFC_EXTERNAL_FAILURE;
		break;

	case 2:
		fill_sy_msg(error_info);
		error_info->code = RFC_ABAP_MESSAGE;
		break;

	default:
		error_info->code = RFC_ABAP_EXCEPTION;
		index = choice - 3;
		RfcGetExceptionDescByIndex(metadata, index, &excep_desc, nullptr);
		memcpy(error_info->key, excep_desc.key, strlenU(excep_desc.key));
		printfU(cU("Do you want to send some SY-MSG variables together with the ABAP Exception? [y/n] "));
		if (on_agree_click())
			fill_sy_msg(error_info);
	}
}

RFC_RC SAP_API CFunctions::repository_lookup(SAP_UC const* function_name, RFC_ATTRIBUTES rfc_attributes, RFC_FUNCTION_DESC_HANDLE* func_desc_handle) {
	RFC_CONNECTION_PARAMETER login_params[1];
	RFC_ERROR_INFO error_info;

	printfU(cU("Repository: %s not yet cached. Checking backend's DDIC.\n"), function_name);
	login_params->name = cU("dest");
	login_params->value = rfc_attributes.sysId;
	const RFC_CONNECTION_HANDLE repo_connection = RfcOpenConnection(login_params, 1, &error_info);
	if (repo_connection == nullptr) {
		printfU(cU("Repository: unable to connect to %s: %s: %s\n"), rfc_attributes.sysId,
			RfcGetRcAsString(error_info.code), error_info.message);
		return RFC_NOT_FOUND;
	}
	*func_desc_handle = RfcGetFunctionDesc(repo_connection, function_name, &error_info);
	RfcCloseConnection(repo_connection, nullptr);
	if (*func_desc_handle == nullptr) {
		printfU(cU("Repository: unable to find metadata for %s: %s: %s\n\n"), function_name,
			RfcGetRcAsString(error_info.code), error_info.key);
		return RFC_NOT_FOUND;
	}

	printfU(cU("Repository: successfully retrieved metadata information for %s\n\n"), function_name);
	return RFC_OK;
}

void CFunctions::fill_sy_msg(RFC_ERROR_INFO* perror_info) {
	SAP_UC value[53];

	printfU(cU("Please enter a value for SY-MSGTY [E/A/X]: "));
	read_value(value, 53);
	value[1] = 0;
	memcpy(perror_info->abapMsgType, value, 2);

	printfU(cU("Please enter a value for SY-MSGID (CHAR20): "));
	read_value(value, 53);
	value[20] = 0;
	memcpy(perror_info->abapMsgClass, value, strlenU(value));

	printfU(cU("Please enter a value for SY-MSGNO [000-999]: "));
	read_value(value, 53);
	value[3] = 0;
	memcpy(perror_info->abapMsgNumber, value, strlenU(value));

	printfU(cU("Please enter a value for SY-MSGV1 (CHAR50): "));
	read_value(value, 53);
	value[50] = 0;
	memcpy(perror_info->abapMsgV1, value, strlenU(value));

	printfU(cU("Please enter a value for SY-MSGV2 (CHAR50): "));
	read_value(value, 53);
	value[50] = 0;
	memcpy(perror_info->abapMsgV2, value, strlenU(value));

	printfU(cU("Please enter a value for SY-MSGV3 (CHAR50): "));
	read_value(value, 53);
	value[50] = 0;
	memcpy(perror_info->abapMsgV3, value, strlenU(value));

	printfU(cU("Please enter a value for SY-MSGV4 (CHAR50): "));
	read_value(value, 53);
	value[50] = 0;
	memcpy(perror_info->abapMsgV4, value, strlenU(value));
}

void CFunctions::fill_structure(const unsigned indent, const RFC_TYPE_DESC_HANDLE type_desc, const RFC_STRUCTURE_HANDLE container) {
	unsigned field_count;
	RFC_RC rc;
	RFC_ERROR_INFO error_info;
	RFC_FIELD_DESC field_desc;
	RFC_STRUCTURE_HANDLE h_structure;
	RFC_TABLE_HANDLE h_table;

	RfcGetFieldCount(type_desc, &field_count, nullptr);

	for (unsigned i = 0; i < field_count; ++i) {
		RfcGetFieldDescByIndex(type_desc, i, &field_desc, nullptr);

		for (unsigned j = 0; j < indent; ++j)
			printfU(cU("\t"));

		switch (field_desc.type) {
		case RFCTYPE_STRUCTURE:
			printfU(cU("%s is a structure. Do you want to fill it? [y/n] "), field_desc.name);

			if (on_agree_click()) {
				rc = RfcGetStructure(container, field_desc.name, &h_structure, &error_info);

				if (rc != RFC_OK) {
					/* Probably out of memory? */
					printfU(cU("Could not create container for %s: %s %s.\n"), field_desc.name,	RfcGetRcAsString(error_info.code), error_info.message);
					printfU(cU("We'll need to do without it...\n"));
					continue;
				}
				fill_structure(indent + 1, field_desc.typeDescHandle, h_structure);
			}
			break;

		case RFCTYPE_TABLE:
			printfU(cU("%s is a table. Do you want to fill it? [y/n] "), field_desc.name);
			if (on_agree_click()) {
				rc = RfcGetTable(container, field_desc.name, &h_table, &error_info);
				if (rc != RFC_OK) {
					/* Probably out of memory? */
					printfU(cU("Could not create container for %s: %s %s.\n"), field_desc.name,
						RfcGetRcAsString(error_info.code), error_info.message);
					printfU(cU("We'll need to do without it...\n"));
					continue;
				}
				fill_table(indent + 1, field_desc.typeDescHandle, h_table);
			}
			break;

		default:
			printfU(cU("%s, %s, Length %d: "), field_desc.name, RfcGetTypeAsString(field_desc.type), field_desc.nucLength);
			read_value(buffer, BUF_SIZE);
			rc = RfcSetString(container, field_desc.name, buffer, strlenU(buffer), &error_info);
			if (rc != RFC_OK) {
				/* Probably the user entered some nonsense. Give him a second chance... */
				printfU(cU("Could not set the value: %s %s.\n"), RfcGetRcAsString(error_info.code), error_info.message);

				printfU(cU("Try again? [y/n] "));
				if (on_agree_click())
					i--;
			}
		}
	}
}

void CFunctions::print_structure(const unsigned indent, const RFC_TYPE_DESC_HANDLE type_desc, const RFC_STRUCTURE_HANDLE container) {
	unsigned field_count, result_len;
	RFC_RC rc;
	RFC_ERROR_INFO error_info;
	RFC_FIELD_DESC field_desc;
	RFC_STRUCTURE_HANDLE structure;
	RFC_TABLE_HANDLE table;

	RfcGetFieldCount(type_desc, &field_count, nullptr);

	for (unsigned i = 0; i < field_count; ++i) {

		RfcGetFieldDescByIndex(type_desc, i, &field_desc, nullptr);
		for (unsigned j = 0; j < indent; ++j)
			printfU(cU("\t"));

		switch (field_desc.type) {
		case RFCTYPE_STRUCTURE:
			printfU(cU("%s is a structure. Do you want to see its values? [y/n] "), field_desc.name);
			if (on_agree_click()) {
				rc = RfcGetStructure(container, field_desc.name, &structure, &error_info);
				if (rc != RFC_OK) {
					/* Probably out of memory? */
					printfU(cU("Could not obtain container for %s: %s %s.\n"), field_desc.name,
						RfcGetRcAsString(error_info.code), error_info.message);
					printfU(cU("We'll need to do without it...\n"));
					continue;
				}
				print_structure(indent + 1, field_desc.typeDescHandle, structure);
			}
			break;

		case RFCTYPE_TABLE:
			rc = RfcGetTable(container, field_desc.name, &table, &error_info);
			if (rc != RFC_OK) {
				/* Probably out of memory? */
				printfU(cU("Could not obtain container for %s: %s %s.\n"), field_desc.name,	RfcGetRcAsString(error_info.code), error_info.message);
				printfU(cU("We'll need to do without it...\n"));
				continue;
			}
			RfcGetRowCount(table, &result_len, nullptr);
			printfU(cU("%s is a table with %d lines. Do you want to see its values? [y/n] "), field_desc.name, result_len);
			if (on_agree_click()) {
				print_table(indent + 1, field_desc.typeDescHandle, table);
			}
			break;

		default:
			printfU(cU("%s, %s, Length %d: "), field_desc.name, RfcGetTypeAsString(field_desc.type), field_desc.nucLength);
			rc = RfcGetString(container, field_desc.name, buffer, BUF_SIZE, &result_len, &error_info);
			if (rc != RFC_OK) {
				/* Probably the buffer is too short? */
				printfU(cU("Could not read the value: %s %s.\n"), RfcGetRcAsString(error_info.code), error_info.message);
				continue;
			}
			printfU(cU("%s\n"), buffer);
		}
	}
}

void CFunctions::fill_table(const unsigned indent, const RFC_TYPE_DESC_HANDLE type_desc, const RFC_TABLE_HANDLE container) {
	unsigned j;
	RFC_ERROR_INFO error_info;

	for (j = 1; j < indent; j++)
		printfU(cU("\t"));

	printfU(cU("Please enter the number of lines: "));
	read_value(buffer, BUF_SIZE);
	const unsigned lines = atoiU(buffer);

	for (unsigned i = 0; i < lines; i++) {
		RFC_STRUCTURE_HANDLE line_handle = RfcAppendNewRow(container, &error_info);
		if (line_handle == nullptr) { // Probably out of memory?!
			printfU(cU("Unable to create a new line: %s %s\n"), RfcGetRcAsString(error_info.code), error_info.message);
			printfU(cU("Skipping the rest.\n"));
			break;
		}
		for (j = 1; j < indent; j++) printfU(cU("\t"));
		printfU(cU("Please enter the values for line %d:\n"), i);
		fill_structure(indent, type_desc, line_handle);
	}
}

void CFunctions::print_table(const unsigned indent, const RFC_TYPE_DESC_HANDLE type_desc, const RFC_TABLE_HANDLE container) {
	unsigned lines, j;
	RFC_ERROR_INFO error_info;

	RfcGetRowCount(container, &lines, &error_info);
	if (lines > 20) {
		for (j = 1; j < indent; ++j) printfU(cU("\t"));
		printfU(cU("That's too many. Only printing the first 20...\n"));
		lines = 20;
	}

	for (int i = 0; i < lines; ++i) {
		RfcMoveTo(container, i, &error_info);
		for (j = 1; j < indent; j++) printfU(cU("\t"));
		printfU(cU("Contents of line %d:\n"), i);
		print_structure(indent, type_desc, container);
	}
}

void CFunctions::check_for_reset(const RFC_CONNECTION_HANDLE h_conn) {
	printfU(cU("Do you want to reset the ABAP session? [y/n] "));
	if (on_agree_click()) RfcResetServerContext(h_conn, nullptr);
}

RFC_RC CFunctions::check_authorization(RFC_ABAP_NAME func_name, RFC_ATTRIBUTES attributes) {
	printfU(cU("AuthorizationCheck: User %s from system %s, client %s, host %s is trying to invoke %s.\n"),
		attributes.user, attributes.sysId, attributes.client, attributes.partnerHost, func_name);
	printfU(cU("Allow access? [y/n] "));
	if (on_agree_click()) return RFC_OK;
	return RFC_EXTERNAL_FAILURE;
}

void CFunctions::read_value(SAP_UC* buffer, const int max) {
	size_t length;
	fgetsU(buffer, max, stdin); /* gets_sU doesn't exist */
	length = strlenU(buffer);

	if (buffer[length - 2] == cU('\r') || buffer[length - 2] == cU('\n'))
		buffer[length - 2] = 0;
	else
		if (buffer[length - 1] == cU('\n') || buffer[length - 1] == cU('\r'))
			buffer[length - 1] = 0;
}

int CFunctions::on_agree_click(void) {
	SAP_UC buf[3];

	read_value(buf, 3);
	if (strncmpU(buf, cU("y"), 3) == 0)
		return 1;

	return 0;
}

void CFunctions::print_imports(RFC_FUNCTION_DESC_HANDLE description, RFC_FUNCTION_HANDLE container) {
	unsigned i, parameterCount;
	RFC_PARAMETER_DESC param_desc;

	RfcGetParameterCount(description, &parameterCount, nullptr);

	printfU(cU("Here are the values of the IMPORTING parameters:\n"));
	for (i = 0; i < parameterCount; i++) {
		RfcGetParameterDescByIndex(description, i, &param_desc, nullptr);
		if (param_desc.direction == RFC_IMPORT) {
			print_parameter(param_desc, container);
		}
	}

	printfU(cU("\nHere are the values of the CHANGING parameters:\n"));
	for (i = 0; i < parameterCount; i++) {
		RfcGetParameterDescByIndex(description, i, &param_desc, nullptr);
		if (param_desc.direction == RFC_CHANGING) {
			print_parameter(param_desc, container);
		}
	}

	printfU(cU("\nHere are the values of the TABLES parameters:\n"));
	for (i = 0; i < parameterCount; i++) {
		RfcGetParameterDescByIndex(description, i, &param_desc, nullptr);
		if (param_desc.direction == RFC_TABLES) {
			print_parameter(param_desc, container);
		}
	}
}

void CFunctions::fill_exports(RFC_FUNCTION_DESC_HANDLE description, RFC_FUNCTION_HANDLE container) {
	int i;
	unsigned parameter_count;
	RFC_PARAMETER_DESC paramDesc;

	RfcGetParameterCount(description, &parameter_count, nullptr);

	printfU(cU("Please enter the EXPORTING parameters:\n"));
	for (i = 0; i < parameter_count; i++) {
		RfcGetParameterDescByIndex(description, i, &paramDesc, nullptr);
		if (paramDesc.direction == RFC_EXPORT) {
			fill_parameter(&i, paramDesc, container);
		}
	}

	printfU(cU("\nPlease enter the CHANGING parameters:\n"));
	for (i = 0; i < parameter_count; i++) {
		RfcGetParameterDescByIndex(description, i, &paramDesc, nullptr);
		if (paramDesc.direction == RFC_CHANGING) {
			fill_parameter(&i, paramDesc, container);
		}
	}

	printfU(cU("\nPlease enter the TABLES parameters:\n"));
	for (i = 0; i < parameter_count; i++) {
		RfcGetParameterDescByIndex(description, i, &paramDesc, nullptr);
		if (paramDesc.direction == RFC_TABLES) {
			fill_parameter(&i, paramDesc, container);
		}
	}
}

void CFunctions::fill_imports(const RFC_FUNCTION_DESC_HANDLE description, const RFC_FUNCTION_HANDLE container) {
	int i;
	unsigned parameter_count;
	RFC_PARAMETER_DESC param_desc;

	RfcGetParameterCount(description, &parameter_count, nullptr);

	printfU(cU("Please enter the IMPORTING parameters:\n"));
	for (i = 0; i < parameter_count; i++) {
		RfcGetParameterDescByIndex(description, i, &param_desc, nullptr);
		if (param_desc.direction == RFC_IMPORT) {
			fill_parameter(&i, param_desc, container);
		}
	}

	printfU(cU("\nPlease enter the CHANGING parameters:\n"));
	for (i = 0; i < parameter_count; i++) {
		RfcGetParameterDescByIndex(description, i, &param_desc, nullptr);
		if (param_desc.direction == RFC_CHANGING) {
			fill_parameter(&i, param_desc, container);
		}
	}

	printfU(cU("\nPlease enter the TABLES parameters:\n"));
	for (i = 0; i < parameter_count; i++) {
		RfcGetParameterDescByIndex(description, i, &param_desc, nullptr);
		if (param_desc.direction == RFC_TABLES) {
			fill_parameter(&i, param_desc, container);
		}
	}
}

void CFunctions::print_exports(const RFC_FUNCTION_DESC_HANDLE description, const RFC_FUNCTION_HANDLE container) {
	unsigned i, parameter_count;
	RFC_PARAMETER_DESC paramDesc;

	RfcGetParameterCount(description, &parameter_count, nullptr);

	printfU(cU("Here are the results of the EXPORTING parameters:\n"));
	for (i = 0; i < parameter_count; i++) {
		RfcGetParameterDescByIndex(description, i, &paramDesc, nullptr);
		if (paramDesc.direction == RFC_EXPORT) {
			print_parameter(paramDesc, container);
		}
	}

	printfU(cU("\nHere are the results of the CHANGING parameters:\n"));
	for (i = 0; i < parameter_count; i++) {
		RfcGetParameterDescByIndex(description, i, &paramDesc, nullptr);
		if (paramDesc.direction == RFC_CHANGING) {
			print_parameter(paramDesc, container);
		}
	}

	printfU(cU("\nHere are the results of the TABLES parameters:\n"));
	for (i = 0; i < parameter_count; i++) {
		RfcGetParameterDescByIndex(description, i, &paramDesc, nullptr);
		if (paramDesc.direction == RFC_TABLES) {
			print_parameter(paramDesc, container);
		}
	}
}