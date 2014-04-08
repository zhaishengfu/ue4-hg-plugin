//-------------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2014 Vadim Macagon
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//-------------------------------------------------------------------------------

#include "MercurialSourceControlPrivatePCH.h"
#include "MercurialSourceControlWorkers.h"
#include "MercurialSourceControlOperationNames.h"
#include "MercurialSourceControlClient.h"
#include "MercurialSourceControlModule.h"
#include "MercurialSourceControlCommand.h"

namespace MercurialSourceControl {

FName FConnectWorker::GetName() const
{
	return OperationNames::Connect;
}

bool FConnectWorker::Execute(FCommand& InCommand)
{
	check(InCommand.GetOperation()->GetName() == OperationNames::Connect);

	return FClient::GetRepositoryRoot(InCommand.GetWorkingDirectory(), RepositoryRoot);
}

bool FConnectWorker::UpdateStates() const
{
	FModule::GetProvider().SetRepositoryRoot(RepositoryRoot);
	return false;
}

FName FUpdateStatusWorker::GetName() const
{
	return OperationNames::UpdateStatus;
}

bool FUpdateStatusWorker::Execute(FCommand& InCommand)
{
	check(InCommand.GetOperation()->GetName() == OperationNames::UpdateStatus);

	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Operation = 
		StaticCastSharedRef<FUpdateStatus>(InCommand.GetOperation());
	
	bool bResult = false;

	if (Operation->ShouldGetOpenedOnly())
	{
		// What Perforce calls "opened" files roughly corresponds to files with an 
		// added/modified/removed status in Mercurial. To keep things simple we'll just update
		// the status of all the files in the current content directory.
		TArray<FString> Files;
		if (InCommand.GetWorkingDirectory() != InCommand.GetContentDirectory())
		{
			FString Directory = InCommand.GetContentDirectory();
			if (FPaths::MakePathRelativeTo(Directory, *InCommand.GetWorkingDirectory()))
			{
				Files.Add(Directory);
			}
			else
			{
				// In this particular case the working directory should be the repository root, 
				// if the content directory can't be made relative to the repository root then
				// it's not in the repository!
				// TODO: localize the error message
				InCommand.ErrorMessages.Add(TEXT("Content directory is not in a repository."));
				return false;
			}
		}
		bResult = FClient::GetFileStates(
			InCommand.GetWorkingDirectory(), Files, FileStates, InCommand.ErrorMessages
		);
	}
	else if (InCommand.GetAbsoluteFiles().Num() > 0)
	{
		bResult = FClient::GetFileStates(
			InCommand.GetWorkingDirectory(), InCommand.GetAbsoluteFiles(), FileStates, 
			InCommand.ErrorMessages
		);
	}
	else // no filenames were provided, so there's nothing to do
	{
		return true;
	}
	
	if (Operation->ShouldUpdateHistory() && (InCommand.GetAbsoluteFiles().Num() > 0))
	{
		bResult = FClient::GetFileHistory(
			InCommand.GetWorkingDirectory(), InCommand.GetAbsoluteFiles(), FileRevisionsMap, 
			InCommand.ErrorMessages
		);
	}
	
	return bResult;
}

bool FUpdateStatusWorker::UpdateStates() const
{
	FProvider& Provider = FModule::GetProvider();
	bool bStatesUpdated = false;
	if (FileStates.Num() > 0)
	{
		bStatesUpdated |= Provider.UpdateFileStateCache(FileStates);
	}
	if (FileRevisionsMap.Num() > 0)
	{
		bStatesUpdated |= Provider.UpdateFileStateCache(FileRevisionsMap);
	}
	return bStatesUpdated;
}

FName FRevertWorker::GetName() const
{
	return OperationNames::Revert;
}

bool FRevertWorker::Execute(FCommand& InCommand)
{
	check(InCommand.GetOperation()->GetName() == OperationNames::Revert);

	bool bResult = FClient::RevertFiles(
		InCommand.GetWorkingDirectory(), InCommand.GetAbsoluteFiles(), InCommand.ErrorMessages
	);

	bResult &= FClient::GetFileStates(
		InCommand.GetWorkingDirectory(), InCommand.GetAbsoluteFiles(), FileStates,
		InCommand.ErrorMessages
	);

	return bResult;
}

bool FRevertWorker::UpdateStates() const
{
	return FModule::GetProvider().UpdateFileStateCache(FileStates);
}

FName FDeleteWorker::GetName() const
{
	return OperationNames::Delete;
}

bool FDeleteWorker::Execute(FCommand& InCommand)
{
	check(InCommand.GetOperation()->GetName() == OperationNames::Delete);

	// NOTE: This will not remove files with an "added" status, but the Editor seems to revert
	//       files before deleting them, so we shouldn't need to handle "added" files here.
	bool bResult = FClient::RemoveFiles(
		InCommand.GetWorkingDirectory(), InCommand.GetAbsoluteFiles(), InCommand.ErrorMessages
	);

	bResult &= FClient::GetFileStates(
		InCommand.GetWorkingDirectory(), InCommand.GetAbsoluteFiles(), FileStates,
		InCommand.ErrorMessages
	);

	return bResult;
}

bool FDeleteWorker::UpdateStates() const
{
	return FModule::GetProvider().UpdateFileStateCache(FileStates);
}

FName FMarkForAddWorker::GetName() const
{
	return OperationNames::MarkForAdd;
}

bool FMarkForAddWorker::Execute(FCommand& InCommand)
{
	check(InCommand.GetOperation()->GetName() == OperationNames::MarkForAdd);

	bool bResult = FClient::AddFiles(
		InCommand.GetWorkingDirectory(), InCommand.GetAbsoluteFiles(), InCommand.ErrorMessages
	);

	bResult &= FClient::GetFileStates(
		InCommand.GetWorkingDirectory(), InCommand.GetAbsoluteFiles(), FileStates,
		InCommand.ErrorMessages
	);

	return bResult;
}

bool FMarkForAddWorker::UpdateStates() const
{
	return FModule::GetProvider().UpdateFileStateCache(FileStates);
}

FName FCheckInWorker::GetName() const
{
	return OperationNames::CheckIn;
}

bool FCheckInWorker::Execute(FCommand& InCommand)
{
	check(InCommand.GetOperation()->GetName() == OperationNames::CheckIn);

	TSharedRef<FCheckIn, ESPMode::ThreadSafe> Operation =
		StaticCastSharedRef<FCheckIn>(InCommand.GetOperation());
	
	bool bResult = FClient::CommitFiles(
		InCommand.GetWorkingDirectory(), InCommand.GetAbsoluteFiles(), Operation->GetDescription(),
		InCommand.ErrorMessages
	);

	bResult &= FClient::GetFileStates(
		InCommand.GetWorkingDirectory(), InCommand.GetAbsoluteFiles(), FileStates,
		InCommand.ErrorMessages
	);

	return bResult;
}

bool FCheckInWorker::UpdateStates() const
{
	return FModule::GetProvider().UpdateFileStateCache(FileStates);
}

} // namespace MercurialSourceControl
