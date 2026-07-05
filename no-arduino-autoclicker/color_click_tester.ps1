param(
    [switch]$SelfTest
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

if ($SelfTest) {
    Write-Host "Self-test OK: Windows Forms helpers loaded."
    exit 0
}

$targetColor = [System.Drawing.Color]::FromArgb(235, 105, 254)
$bgColor = [System.Drawing.Color]::FromArgb(17, 19, 24)
$fgColor = [System.Drawing.Color]::FromArgb(238, 241, 245)
$mutedColor = [System.Drawing.Color]::FromArgb(184, 192, 204)

$totalClicks = 0
$targetClicks = 0
$missedClicks = 0
$autoPull = $false

$form = New-Object System.Windows.Forms.Form
$form.Text = "No-Arduino Click Tester"
$form.Size = New-Object System.Drawing.Size(560, 520)
$form.MinimumSize = New-Object System.Drawing.Size(460, 430)
$form.StartPosition = "CenterScreen"
$form.BackColor = $bgColor

$title = New-Object System.Windows.Forms.Label
$title.Text = "Click tester"
$title.Font = New-Object System.Drawing.Font("Segoe UI", 16, [System.Drawing.FontStyle]::Bold)
$title.ForeColor = $fgColor
$title.BackColor = $bgColor
$title.AutoSize = $true
$title.Location = New-Object System.Drawing.Point(18, 18)
$form.Controls.Add($title)

$colorLabel = New-Object System.Windows.Forms.Label
$colorLabel.Text = "Target color: #EB69FE / RGB(235, 105, 254)"
$colorLabel.Font = New-Object System.Drawing.Font("Segoe UI", 10)
$colorLabel.ForeColor = $mutedColor
$colorLabel.BackColor = $bgColor
$colorLabel.AutoSize = $true
$colorLabel.Location = New-Object System.Drawing.Point(20, 56)
$form.Controls.Add($colorLabel)

$target = New-Object System.Windows.Forms.Panel
$target.BackColor = $targetColor
$target.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
$target.Size = New-Object System.Drawing.Size(260, 260)
$target.Location = New-Object System.Drawing.Point(140, 92)
$form.Controls.Add($target)

$targetText = New-Object System.Windows.Forms.Label
$targetText.Text = "PUT CURSOR HERE`r`nTHEN PRESS F8"
$targetText.Font = New-Object System.Drawing.Font("Segoe UI", 18, [System.Drawing.FontStyle]::Bold)
$targetText.ForeColor = $bgColor
$targetText.BackColor = $targetColor
$targetText.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
$targetText.Dock = [System.Windows.Forms.DockStyle]::Fill
$target.Controls.Add($targetText)

$stats = New-Object System.Windows.Forms.Label
$stats.Font = New-Object System.Drawing.Font("Segoe UI", 12)
$stats.ForeColor = $fgColor
$stats.BackColor = $bgColor
$stats.AutoSize = $true
$stats.Location = New-Object System.Drawing.Point(20, 370)
$form.Controls.Add($stats)

function Update-Stats {
    $stats.Text = "Clicks on target: $targetClicks    Missed/window clicks: $missedClicks    Total: $totalClicks"
}

function Pull-CursorToTarget {
    $center = $target.PointToScreen(
        [System.Drawing.Point]::new(
            [int]($target.Width / 2),
            [int]($target.Height / 2)
        )
    )
    [System.Windows.Forms.Cursor]::Position = $center
}

function Update-AutoPullButton {
    if ($script:autoPull) {
        $autoPullButton.Text = "Auto-pull: on"
    } else {
        $autoPullButton.Text = "Auto-pull: off"
    }
}

function Toggle-AutoPull {
    $script:autoPull = -not $script:autoPull
    Update-AutoPullButton
}

function Add-TargetClick {
    $script:totalClicks += 1
    $script:targetClicks += 1
    Update-Stats
}

function Add-MissedClick {
    $script:totalClicks += 1
    $script:missedClicks += 1
    Update-Stats
}

$target.Add_Click({ Add-TargetClick })
$targetText.Add_Click({ Add-TargetClick })
$form.Add_Click({ Add-MissedClick })

$hint = New-Object System.Windows.Forms.Label
$hint.Text = "F6 pulls cursor once. F7 toggles local auto-pull. F8 toggles auto-clicker. F9 sends one click. F10 closes the auto-clicker window."
$hint.Font = New-Object System.Drawing.Font("Segoe UI", 9)
$hint.ForeColor = $mutedColor
$hint.BackColor = $bgColor
$hint.AutoSize = $true
$hint.Location = New-Object System.Drawing.Point(20, 406)
$form.Controls.Add($hint)

$reset = New-Object System.Windows.Forms.Button
$reset.Text = "Reset counter"
$reset.Size = New-Object System.Drawing.Size(110, 30)
$reset.Location = New-Object System.Drawing.Point(20, 438)
$reset.Add_Click({
    $script:totalClicks = 0
    $script:targetClicks = 0
    $script:missedClicks = 0
    Update-Stats
})
$form.Controls.Add($reset)

$topMost = New-Object System.Windows.Forms.Button
$topMost.Text = "Keep on top"
$topMost.Size = New-Object System.Drawing.Size(110, 30)
$topMost.Location = New-Object System.Drawing.Point(140, 438)
$topMost.Add_Click({
    $form.TopMost = -not $form.TopMost
})
$form.Controls.Add($topMost)

$pull = New-Object System.Windows.Forms.Button
$pull.Text = "Pull cursor"
$pull.Size = New-Object System.Drawing.Size(110, 30)
$pull.Location = New-Object System.Drawing.Point(260, 438)
$pull.Add_Click({ Pull-CursorToTarget })
$form.Controls.Add($pull)

$autoPullButton = New-Object System.Windows.Forms.Button
$autoPullButton.Text = "Auto-pull: off"
$autoPullButton.Size = New-Object System.Drawing.Size(120, 30)
$autoPullButton.Location = New-Object System.Drawing.Point(380, 438)
$autoPullButton.Add_Click({ Toggle-AutoPull })
$form.Controls.Add($autoPullButton)

$form.KeyPreview = $true
$form.Add_KeyDown({
    param($sender, $eventArgs)
    if ($eventArgs.KeyCode -eq [System.Windows.Forms.Keys]::F6) {
        Pull-CursorToTarget
        $eventArgs.Handled = $true
    } elseif ($eventArgs.KeyCode -eq [System.Windows.Forms.Keys]::F7) {
        Toggle-AutoPull
        $eventArgs.Handled = $true
    }
})

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 50
$timer.Add_Tick({
    if ($script:autoPull) {
        Pull-CursorToTarget
    }
})
$timer.Start()

Update-Stats
[void]$form.ShowDialog()
